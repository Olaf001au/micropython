/*
 * warpig_httpd — Native HTTP server for MicroPython via ESP-IDF httpd.
 *
 * Wraps esp_http_server for fast request handling. Routes call Python handlers.
 * Request parsing (method, path, headers, body) happens in C.
 *
 * Python API:
 *   import warpig_httpd
 *
 *   def handler(req):
 *       print(req.method, req.path, req.body)
 *       return warpig_httpd.response(200, "application/json", '{"ok":true}')
 *
 *   srv = warpig_httpd.Server(port=8080)
 *   srv.route("GET", "/status", handler)
 *   srv.route("PUT", "/files/<path>", file_handler)
 *   srv.start()
 *   # Server runs on its own FreeRTOS task — non-blocking
 *
 * Copyright (c) 2026 WarpPig Project — MIT License
 */

#include "py/runtime.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "py/mperrno.h"
#include "py/gc.h"
#include "esp_http_server.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "warpig_httpd";

#define MAX_ROUTES 32

// ── Request object (read-only, populated by C) ──────────────────────────────

typedef struct {
    mp_obj_base_t base;
    mp_obj_t method;     // str
    mp_obj_t path;       // str
    mp_obj_t query;      // str or None
    mp_obj_t body;       // bytes or None
    mp_obj_t headers;    // dict
    httpd_req_t *raw_req; // internal — for sending response
} warpig_request_t;

static void request_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    warpig_request_t *self = MP_OBJ_TO_PTR(self_in);
    if (dest[0] != MP_OBJ_NULL) return;
    if (attr == MP_QSTR_method)  dest[0] = self->method;
    else if (attr == MP_QSTR_path)    dest[0] = self->path;
    else if (attr == MP_QSTR_query)   dest[0] = self->query;
    else if (attr == MP_QSTR_body)    dest[0] = self->body;
    else if (attr == MP_QSTR_headers) dest[0] = self->headers;
}

MP_DEFINE_CONST_OBJ_TYPE(
    warpig_request_type,
    MP_QSTR_Request,
    MP_TYPE_FLAG_NONE,
    attr, request_attr
);

// ── Response helper ─────────────────────────────────────────────────────────

// warpig_httpd.response(status, content_type, body)
static mp_obj_t warpig_httpd_response(mp_obj_t status_in, mp_obj_t ctype_in,
                                       mp_obj_t body_in) {
    mp_obj_t items[] = { status_in, ctype_in, body_in };
    return mp_obj_new_tuple(3, items);
}
static MP_DEFINE_CONST_FUN_OBJ_3(warpig_httpd_response_obj, warpig_httpd_response);

// ── Route table ─────────────────────────────────────────────────────────────

typedef struct {
    char uri[128];
    int method;            // HTTP_GET, HTTP_POST, etc.
    mp_obj_t handler;      // Python callable
} route_entry_t;

typedef struct {
    mp_obj_base_t base;
    httpd_handle_t handle;
    int port;
    route_entry_t routes[MAX_ROUTES];
    int route_count;
    bool running;
} warpig_server_t;

// ── Generic HTTP handler (C→Python bridge) ──────────────────────────────────

static esp_err_t generic_handler(httpd_req_t *req) {
    route_entry_t *route = (route_entry_t *)req->user_ctx;

    // Build Request object
    warpig_request_t *py_req = m_new_obj(warpig_request_t);
    py_req->base.type = &warpig_request_type;
    py_req->raw_req = req;

    // Method
    switch (req->method) {
        case HTTP_GET:    py_req->method = MP_OBJ_NEW_QSTR(MP_QSTR_GET); break;
        case HTTP_POST:   py_req->method = MP_OBJ_NEW_QSTR(MP_QSTR_POST); break;
        case HTTP_PUT:    py_req->method = MP_OBJ_NEW_QSTR(MP_QSTR_PUT); break;
        case HTTP_DELETE: py_req->method = MP_OBJ_NEW_QSTR(MP_QSTR_DELETE); break;
        default:          py_req->method = mp_obj_new_str("OTHER", 5); break;
    }

    // Path
    // Strip query string from URI for clean path
    const char *uri = req->uri;
    const char *q = strchr(uri, '?');
    if (q) {
        py_req->path = mp_obj_new_str(uri, q - uri);
        py_req->query = mp_obj_new_str(q + 1, strlen(q + 1));
    } else {
        py_req->path = mp_obj_new_str(uri, strlen(uri));
        py_req->query = mp_const_none;
    }

    // Body (read into buffer)
    size_t content_len = req->content_len;
    if (content_len > 0 && content_len < 65536) {
        char *buf = m_malloc(content_len + 1);
        int received = httpd_req_recv(req, buf, content_len);
        if (received > 0) {
            buf[received] = '\0';
            py_req->body = mp_obj_new_bytes((const byte *)buf, received);
        } else {
            py_req->body = mp_const_none;
        }
        m_free(buf);
    } else {
        py_req->body = mp_const_none;
    }

    // Headers (extract common ones)
    py_req->headers = mp_obj_new_dict(4);
    char hdr_buf[256];
    static const char *hdr_names[] = {
        "Content-Type", "Authorization", "X-Token", "Host", NULL
    };
    for (int i = 0; hdr_names[i]; i++) {
        if (httpd_req_get_hdr_value_str(req, hdr_names[i], hdr_buf, sizeof(hdr_buf)) == ESP_OK) {
            mp_obj_dict_store(py_req->headers,
                              mp_obj_new_str(hdr_names[i], strlen(hdr_names[i])),
                              mp_obj_new_str(hdr_buf, strlen(hdr_buf)));
        }
    }

    // Call Python handler
    mp_obj_t result = mp_const_none;
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        result = mp_call_function_1(route->handler, MP_OBJ_FROM_PTR(py_req));
        nlr_pop();
    } else {
        // Python exception — return 500
        ESP_LOGE(TAG, "Handler exception for %s", uri);
        mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Handler error");
        return ESP_OK;
    }

    // Parse response tuple: (status, content_type, body)
    if (mp_obj_is_type(result, &mp_type_tuple)) {
        mp_obj_tuple_t *tup = MP_OBJ_TO_PTR(result);
        if (tup->len >= 3) {
            int status = mp_obj_get_int(tup->items[0]);
            const char *ctype = mp_obj_str_get_str(tup->items[1]);
            size_t body_len;
            const char *body = mp_obj_str_get_data(tup->items[2], &body_len);

            char status_str[4];
            snprintf(status_str, sizeof(status_str), "%d", status);
            httpd_resp_set_status(req, status_str);
            httpd_resp_set_type(req, ctype);
            httpd_resp_send(req, body, body_len);
            return ESP_OK;
        }
    }

    // Default: 204 No Content
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// ── Server methods ──────────────────────────────────────────────────────────

// srv.route(method_str, uri, handler)
static mp_obj_t server_route(size_t n_args, const mp_obj_t *args) {
    warpig_server_t *self = MP_OBJ_TO_PTR(args[0]);
    const char *method_str = mp_obj_str_get_str(args[1]);
    const char *uri = mp_obj_str_get_str(args[2]);
    mp_obj_t handler = args[3];

    if (self->route_count >= MAX_ROUTES) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("max routes reached"));
    }

    route_entry_t *r = &self->routes[self->route_count];
    strncpy(r->uri, uri, sizeof(r->uri) - 1);
    r->handler = handler;

    if (strcmp(method_str, "GET") == 0) r->method = HTTP_GET;
    else if (strcmp(method_str, "POST") == 0) r->method = HTTP_POST;
    else if (strcmp(method_str, "PUT") == 0) r->method = HTTP_PUT;
    else if (strcmp(method_str, "DELETE") == 0) r->method = HTTP_DELETE;
    else r->method = HTTP_GET;

    self->route_count++;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(server_route_obj, 4, 4, server_route);

// srv.start()
static mp_obj_t server_start(mp_obj_t self_in) {
    warpig_server_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->running) return mp_const_none;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = self->port;
    config.max_uri_handlers = MAX_ROUTES;
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&self->handle, &config);
    if (err != ESP_OK) {
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("httpd start failed: %s"),
                          esp_err_to_name(err));
    }

    // Register all routes
    for (int i = 0; i < self->route_count; i++) {
        httpd_uri_t uri_handler = {
            .uri = self->routes[i].uri,
            .method = self->routes[i].method,
            .handler = generic_handler,
            .user_ctx = &self->routes[i],
        };
        httpd_register_uri_handler(self->handle, &uri_handler);
    }

    self->running = true;
    ESP_LOGI(TAG, "HTTP server started on port %d (%d routes)", self->port, self->route_count);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(server_start_obj, server_start);

// srv.stop()
static mp_obj_t server_stop(mp_obj_t self_in) {
    warpig_server_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->running && self->handle) {
        httpd_stop(self->handle);
        self->handle = NULL;
        self->running = false;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(server_stop_obj, server_stop);

// ── Server constructor ──────────────────────────────────────────────────────

static mp_obj_t server_make_new(const mp_obj_type_t *type, size_t n_args,
                                 size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_port };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_port, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 80} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args,
                               MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    warpig_server_t *self = mp_obj_malloc(warpig_server_t, type);
    self->port = args[ARG_port].u_int;
    self->handle = NULL;
    self->route_count = 0;
    self->running = false;
    return MP_OBJ_FROM_PTR(self);
}

static const mp_rom_map_elem_t server_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_route), MP_ROM_PTR(&server_route_obj) },
    { MP_ROM_QSTR(MP_QSTR_start), MP_ROM_PTR(&server_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop),  MP_ROM_PTR(&server_stop_obj) },
};
static MP_DEFINE_CONST_DICT(server_locals, server_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    warpig_server_type,
    MP_QSTR_Server,
    MP_TYPE_FLAG_NONE,
    make_new, server_make_new,
    locals_dict, &server_locals
);

// ── Module definition ───────────────────────────────────────────────────────

static const mp_rom_map_elem_t warpig_httpd_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),  MP_ROM_QSTR(MP_QSTR_warpig_httpd) },
    { MP_ROM_QSTR(MP_QSTR_Server),    MP_ROM_PTR(&warpig_server_type) },
    { MP_ROM_QSTR(MP_QSTR_response),  MP_ROM_PTR(&warpig_httpd_response_obj) },
};
static MP_DEFINE_CONST_DICT(warpig_httpd_globals, warpig_httpd_globals_table);

const mp_obj_module_t warpig_httpd_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&warpig_httpd_globals,
};

MP_REGISTER_MODULE(MP_QSTR_warpig_httpd, warpig_httpd_module);
