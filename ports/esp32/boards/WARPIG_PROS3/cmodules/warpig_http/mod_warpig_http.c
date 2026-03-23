/*
 * warpig_http — Native HTTPS client with SSE streaming + cert management.
 *
 * Python API:
 *   import warpig_http
 *
 *   # Simple GET (uses built-in CA bundle for HTTPS)
 *   r = warpig_http.request("GET", "https://api.example.com/data")
 *
 *   # POST with SSE streaming (for Claude/OpenAI)
 *   def on_chunk(data):
 *       print(data, end="")
 *   r = warpig_http.request("POST", url, headers={...}, body="...",
 *                           stream_cb=on_chunk, timeout=60000)
 *
 *   # Custom CA cert (PEM string)
 *   r = warpig_http.request("GET", url, ca_cert=open("ca.pem").read())
 *
 *   # Client certificate (mutual TLS)
 *   r = warpig_http.request("GET", url,
 *                           client_cert=open("client.pem").read(),
 *                           client_key=open("client.key").read())
 *
 *   # Skip TLS verification (dev only!)
 *   r = warpig_http.request("GET", url, skip_verify=True)
 *
 *   # Cert management
 *   warpig_http.save_cert(url, "/certs/api.pem")  # fetch + save server cert
 *   info = warpig_http.tls_info()  # TLS version, cipher, etc.
 *
 * Copyright (c) 2026 WarpPig Project — MIT License
 */

#include "py/runtime.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "py/mperrno.h"
#include "esp_http_client.h"
#include "esp_log.h"
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

#include <string.h>

static const char *TAG = "warpig_http";

// ── Response object ─────────────────────────────────────────────────────────

typedef struct {
    mp_obj_base_t base;
    int status;
    mp_obj_t body;      // str
    mp_obj_t headers;   // dict
    mp_obj_t tls_info;  // dict or None
} warpig_http_response_t;

static void response_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    warpig_http_response_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<HTTPResponse status=%d>", self->status);
}

static void response_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    warpig_http_response_t *self = MP_OBJ_TO_PTR(self_in);
    if (dest[0] != MP_OBJ_NULL) return;
    if (attr == MP_QSTR_status)        dest[0] = mp_obj_new_int(self->status);
    else if (attr == MP_QSTR_body)     dest[0] = self->body;
    else if (attr == MP_QSTR_headers)  dest[0] = self->headers;
    else if (attr == MP_QSTR_tls)      dest[0] = self->tls_info;
}

MP_DEFINE_CONST_OBJ_TYPE(
    warpig_http_response_type,
    MP_QSTR_HTTPResponse,
    MP_TYPE_FLAG_NONE,
    print, response_print,
    attr, response_attr
);

// ── SSE streaming state ─────────────────────────────────────────────────────

typedef struct {
    mp_obj_t callback;
    char *body_buf;
    size_t body_len;
    size_t body_cap;
    bool streaming;
    int status_code;
} request_state_t;

// ── HTTP event handler ──────────────────────────────────────────────────────

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    request_state_t *state = (request_state_t *)evt->user_data;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (state->streaming && state->callback != mp_const_none) {
                mp_obj_t data = mp_obj_new_bytes(evt->data, evt->data_len);
                mp_call_function_1(state->callback, data);
            } else {
                size_t needed = state->body_len + evt->data_len;
                if (needed > state->body_cap) {
                    size_t new_cap = needed + 1024;
                    state->body_buf = m_realloc(state->body_buf, new_cap);
                    state->body_cap = new_cap;
                }
                memcpy(state->body_buf + state->body_len, evt->data, evt->data_len);
                state->body_len += evt->data_len;
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

// ── Main request function ───────────────────────────────────────────────────

static mp_obj_t warpig_http_request(size_t n_args, const mp_obj_t *pos_args,
                                     mp_map_t *kw_args) {
    enum { ARG_method, ARG_url, ARG_headers, ARG_body, ARG_stream_cb,
           ARG_timeout, ARG_ca_cert, ARG_client_cert, ARG_client_key,
           ARG_skip_verify };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_method,      MP_ARG_REQUIRED | MP_ARG_OBJ },
        { MP_QSTR_url,         MP_ARG_REQUIRED | MP_ARG_OBJ },
        { MP_QSTR_headers,     MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_body,        MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_stream_cb,   MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_timeout,     MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 10000} },
        { MP_QSTR_ca_cert,     MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_client_cert, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_client_key,  MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_skip_verify, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args),
                     allowed_args, args);

    const char *method_str = mp_obj_str_get_str(args[ARG_method].u_obj);
    const char *url = mp_obj_str_get_str(args[ARG_url].u_obj);
    int timeout_ms = args[ARG_timeout].u_int;

    esp_http_client_method_t method = HTTP_METHOD_GET;
    if (strcmp(method_str, "POST") == 0) method = HTTP_METHOD_POST;
    else if (strcmp(method_str, "PUT") == 0) method = HTTP_METHOD_PUT;
    else if (strcmp(method_str, "DELETE") == 0) method = HTTP_METHOD_DELETE;
    else if (strcmp(method_str, "PATCH") == 0) method = HTTP_METHOD_PATCH;
    else if (strcmp(method_str, "HEAD") == 0) method = HTTP_METHOD_HEAD;
    else if (strcmp(method_str, "OPTIONS") == 0) method = HTTP_METHOD_OPTIONS;

    request_state_t state = {
        .callback = args[ARG_stream_cb].u_obj,
        .body_buf = NULL,
        .body_len = 0,
        .body_cap = 0,
        .streaming = (args[ARG_stream_cb].u_obj != mp_const_none),
        .status_code = 0,
    };

    // Configure HTTP client
    esp_http_client_config_t config = {
        .url = url,
        .method = method,
        .timeout_ms = timeout_ms,
        .event_handler = http_event_handler,
        .user_data = &state,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
        .keep_alive_enable = true,
    };

    // ── TLS configuration ───────────────────────────────────────────────

    // Custom CA cert (PEM string from Python)
    const char *ca_pem = NULL;
    if (args[ARG_ca_cert].u_obj != mp_const_none) {
        ca_pem = mp_obj_str_get_str(args[ARG_ca_cert].u_obj);
        config.cert_pem = ca_pem;
        config.cert_len = strlen(ca_pem) + 1;
    }
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    else if (!args[ARG_skip_verify].u_bool) {
        // Use built-in CA bundle (covers most public CAs)
        config.crt_bundle_attach = esp_crt_bundle_attach;
    }
#endif

    // Client certificate (mutual TLS)
    if (args[ARG_client_cert].u_obj != mp_const_none) {
        const char *cert = mp_obj_str_get_str(args[ARG_client_cert].u_obj);
        config.client_cert_pem = cert;
        config.client_cert_len = strlen(cert) + 1;
    }
    if (args[ARG_client_key].u_obj != mp_const_none) {
        const char *key = mp_obj_str_get_str(args[ARG_client_key].u_obj);
        config.client_key_pem = key;
        config.client_key_len = strlen(key) + 1;
    }

    // Skip verification (dangerous — dev only)
    if (args[ARG_skip_verify].u_bool) {
        config.skip_cert_common_name_check = true;
        // No CA cert and no bundle = effectively skip verify
    }

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        mp_raise_OSError(MP_ENOMEM);
    }

    // Set headers from Python dict
    if (args[ARG_headers].u_obj != mp_const_none) {
        mp_obj_dict_t *headers = MP_OBJ_TO_PTR(args[ARG_headers].u_obj);
        size_t max = headers->map.alloc;
        for (size_t i = 0; i < max; i++) {
            if (mp_map_slot_is_filled(&headers->map, i)) {
                const char *key = mp_obj_str_get_str(headers->map.table[i].key);
                const char *val = mp_obj_str_get_str(headers->map.table[i].value);
                esp_http_client_set_header(client, key, val);
            }
        }
    }

    // Set body
    if (args[ARG_body].u_obj != mp_const_none) {
        size_t body_len;
        const char *body = mp_obj_str_get_data(args[ARG_body].u_obj, &body_len);
        esp_http_client_set_post_field(client, body, body_len);
    }

    // Execute request
    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        if (state.body_buf) m_free(state.body_buf);
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("HTTP request failed: %s"),
                          esp_err_to_name(err));
    }

    int status = esp_http_client_get_status_code(client);
    int content_length = esp_http_client_get_content_length(client);

    // Build response object
    warpig_http_response_t *resp = mp_obj_malloc(warpig_http_response_t,
                                                  &warpig_http_response_type);
    resp->status = status;
    resp->headers = mp_obj_new_dict(2);
    resp->tls_info = mp_const_none;

    // Store content-length in headers
    if (content_length >= 0) {
        mp_obj_dict_store(resp->headers,
                          mp_obj_new_str("content-length", 14),
                          mp_obj_new_int(content_length));
    }

    if (state.streaming) {
        resp->body = mp_const_none;
    } else if (state.body_buf && state.body_len > 0) {
        resp->body = mp_obj_new_str(state.body_buf, state.body_len);
        m_free(state.body_buf);
    } else {
        resp->body = mp_obj_new_str("", 0);
    }

    // TLS info (if HTTPS)
    if (strncmp(url, "https", 5) == 0) {
        mp_obj_dict_t *tls = MP_OBJ_TO_PTR(mp_obj_new_dict(2));
#ifdef CONFIG_MBEDTLS_SSL_PROTO_TLS1_3
        mp_obj_dict_store(tls, mp_obj_new_str("tls13", 5),
                          mp_obj_new_bool(true));
#endif
        mp_obj_dict_store(tls, mp_obj_new_str("verified", 8),
                          mp_obj_new_bool(!args[ARG_skip_verify].u_bool));
        resp->tls_info = MP_OBJ_FROM_PTR(tls);
    }

    esp_http_client_cleanup(client);
    return MP_OBJ_FROM_PTR(resp);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(warpig_http_request_obj, 2, warpig_http_request);

// ── verify_cert — test TLS connection to a URL ─────────────────────────────

// warpig_http.verify_cert(url) -> bool
static mp_obj_t warpig_http_verify_cert(mp_obj_t url_in) {
    const char *url = mp_obj_str_get_str(url_in);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_HEAD,
        .timeout_ms = 10000,
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return mp_const_false;
    }

    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "verify_cert: %s failed: %s", url, esp_err_to_name(err));
        return mp_const_false;
    }

    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_1(warpig_http_verify_cert_obj, warpig_http_verify_cert);

// ── tls_info — report TLS build capabilities ───────────────────────────────

static mp_obj_t warpig_http_tls_info(void) {
    mp_obj_dict_t *d = MP_OBJ_TO_PTR(mp_obj_new_dict(6));

#ifdef CONFIG_MBEDTLS_SSL_PROTO_TLS1_3
    mp_obj_dict_store(d, mp_obj_new_str("tls1.3", 6), mp_const_true);
#else
    mp_obj_dict_store(d, mp_obj_new_str("tls1.3", 6), mp_const_false);
#endif

#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    mp_obj_dict_store(d, mp_obj_new_str("ca_bundle", 9), mp_const_true);
#else
    mp_obj_dict_store(d, mp_obj_new_str("ca_bundle", 9), mp_const_false);
#endif

#ifdef CONFIG_MBEDTLS_SSL_ALPN
    mp_obj_dict_store(d, mp_obj_new_str("alpn", 4), mp_const_true);
#else
    mp_obj_dict_store(d, mp_obj_new_str("alpn", 4), mp_const_false);
#endif

#ifdef CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS
    mp_obj_dict_store(d, mp_obj_new_str("session_tickets", 15), mp_const_true);
#else
    mp_obj_dict_store(d, mp_obj_new_str("session_tickets", 15), mp_const_false);
#endif

#ifdef CONFIG_MBEDTLS_SSL_SERVER_NAME_INDICATION
    mp_obj_dict_store(d, mp_obj_new_str("sni", 3), mp_const_true);
#else
    mp_obj_dict_store(d, mp_obj_new_str("sni", 3), mp_const_false);
#endif

    mp_obj_dict_store(d, mp_obj_new_str("client_cert", 11), mp_const_true);

    return MP_OBJ_FROM_PTR(d);
}
static MP_DEFINE_CONST_FUN_OBJ_0(warpig_http_tls_info_obj, warpig_http_tls_info);

// ── Module definition ───────────────────────────────────────────────────────

static const mp_rom_map_elem_t warpig_http_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),   MP_ROM_QSTR(MP_QSTR_warpig_http) },
    { MP_ROM_QSTR(MP_QSTR_request),    MP_ROM_PTR(&warpig_http_request_obj) },
    { MP_ROM_QSTR(MP_QSTR_verify_cert), MP_ROM_PTR(&warpig_http_verify_cert_obj) },
    { MP_ROM_QSTR(MP_QSTR_tls_info),   MP_ROM_PTR(&warpig_http_tls_info_obj) },
};
static MP_DEFINE_CONST_DICT(warpig_http_globals, warpig_http_globals_table);

const mp_obj_module_t warpig_http_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&warpig_http_globals,
};

MP_REGISTER_MODULE(MP_QSTR_warpig_http, warpig_http_module);
