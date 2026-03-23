/*
 * warpig_rtos — FreeRTOS primitives exposed to MicroPython.
 *
 * Gives Python code direct access to:
 *   - Task pinning to specific cores (ESP32-S3 has 2 cores)
 *   - Task info (name, state, priority, stack high water mark, core affinity)
 *   - Semaphores (binary + counting)
 *   - Queues (inter-task communication)
 *   - ISR-safe notifications
 *   - CPU frequency control
 *   - Heap stats per capability (internal SRAM, PSRAM, DMA-capable)
 *
 * Python API:
 *   import warpig_rtos as rtos
 *
 *   # Task info
 *   for t in rtos.tasks():
 *       print(t)  # {"name": "mp_task", "state": "Running", "prio": 1,
 *                 #  "stack_hwm": 2048, "core": 0}
 *
 *   # Pin current thread to core 1
 *   rtos.pin_to_core(1)
 *
 *   # Semaphore
 *   sem = rtos.Semaphore()
 *   sem.acquire(timeout_ms=1000)
 *   sem.release()
 *
 *   # Queue (typed bytes, fixed-size items)
 *   q = rtos.Queue(length=10, item_size=64)
 *   q.put(b"sensor_data_here")
 *   data = q.get(timeout_ms=500)
 *
 *   # System info
 *   print(rtos.cpu_freq())       # 240000000
 *   print(rtos.free_heap())      # {"internal": 234567, "psram": 8000000, "dma": 123456}
 *   print(rtos.task_count())     # 12
 *   print(rtos.uptime_ms())      # 123456789
 *
 * Copyright (c) 2026 WarpPig Project — MIT License
 */

#include "py/runtime.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "py/mperrno.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/idf_additions.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "esp_clk_tree.h"
#include "esp_task_wdt.h"
#include "esp_sleep.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include "soc/soc_caps.h"

// ── Task info ───────────────────────────────────────────────────────────────

static const char *task_state_str(eTaskState s) {
    switch (s) {
        case eRunning:   return "Running";
        case eReady:     return "Ready";
        case eBlocked:   return "Blocked";
        case eSuspended: return "Suspended";
        case eDeleted:   return "Deleted";
        default:         return "Unknown";
    }
}

// rtos.tasks() -> list of dicts
static mp_obj_t rtos_tasks(void) {
    UBaseType_t count = uxTaskGetNumberOfTasks();
    TaskStatus_t *task_array = m_malloc(count * sizeof(TaskStatus_t));
    uint32_t total_runtime;

    UBaseType_t got = uxTaskGetSystemState(task_array, count, &total_runtime);

    mp_obj_list_t *result = MP_OBJ_TO_PTR(mp_obj_new_list(0, NULL));

    for (UBaseType_t i = 0; i < got; i++) {
        mp_obj_dict_t *d = MP_OBJ_TO_PTR(mp_obj_new_dict(6));

        mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_name),
                          mp_obj_new_str(task_array[i].pcTaskName,
                                         strlen(task_array[i].pcTaskName)));
        mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_state),
                          mp_obj_new_str(task_state_str(task_array[i].eCurrentState),
                                         strlen(task_state_str(task_array[i].eCurrentState))));
        mp_obj_dict_store(d, mp_obj_new_str("prio", 4),
                          mp_obj_new_int(task_array[i].uxCurrentPriority));
        mp_obj_dict_store(d, mp_obj_new_str("stack_hwm", 9),
                          mp_obj_new_int(task_array[i].usStackHighWaterMark));
        mp_obj_dict_store(d, mp_obj_new_str("core", 4),
                          mp_obj_new_int(xTaskGetAffinity(task_array[i].xHandle)));
        mp_obj_dict_store(d, mp_obj_new_str("runtime", 7),
                          mp_obj_new_int(task_array[i].ulRunTimeCounter));

        mp_obj_list_append(result, MP_OBJ_FROM_PTR(d));
    }

    m_free(task_array);
    return MP_OBJ_FROM_PTR(result);
}
static MP_DEFINE_CONST_FUN_OBJ_0(rtos_tasks_obj, rtos_tasks);

// rtos.task_count() -> int
static mp_obj_t rtos_task_count(void) {
    return mp_obj_new_int(uxTaskGetNumberOfTasks());
}
static MP_DEFINE_CONST_FUN_OBJ_0(rtos_task_count_obj, rtos_task_count);

// rtos.pin_to_core(core_id) — pin current task to core 0 or 1
static mp_obj_t rtos_pin_to_core(mp_obj_t core_in) {
    int core = mp_obj_get_int(core_in);
    if (core < 0 || core > 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("core must be 0 or 1"));
    }
    // ESP-IDF doesn't allow changing affinity after creation.
    // This is informational — use _thread with core pinning instead.
    // For now, return the current core.
    mp_raise_msg(&mp_type_NotImplementedError,
                 MP_ERROR_TEXT("use _thread.start_new_thread() with core arg"));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(rtos_pin_to_core_obj, rtos_pin_to_core);

// ── Heap info ───────────────────────────────────────────────────────────────

// rtos.free_heap() -> dict
static mp_obj_t rtos_free_heap(void) {
    mp_obj_dict_t *d = MP_OBJ_TO_PTR(mp_obj_new_dict(4));

    mp_obj_dict_store(d, mp_obj_new_str("total", 5),
                      mp_obj_new_int(esp_get_free_heap_size()));
    mp_obj_dict_store(d, mp_obj_new_str("internal", 8),
                      mp_obj_new_int(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    mp_obj_dict_store(d, mp_obj_new_str("psram", 5),
                      mp_obj_new_int(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    mp_obj_dict_store(d, mp_obj_new_str("dma", 3),
                      mp_obj_new_int(heap_caps_get_free_size(MALLOC_CAP_DMA)));
    mp_obj_dict_store(d, mp_obj_new_str("min_ever", 8),
                      mp_obj_new_int(esp_get_minimum_free_heap_size()));

    return MP_OBJ_FROM_PTR(d);
}
static MP_DEFINE_CONST_FUN_OBJ_0(rtos_free_heap_obj, rtos_free_heap);

// ── Timing ──────────────────────────────────────────────────────────────────

// rtos.uptime_ms() -> int (64-bit microsecond timer / 1000)
static mp_obj_t rtos_uptime_ms(void) {
    return mp_obj_new_int_from_ull(esp_timer_get_time() / 1000);
}
static MP_DEFINE_CONST_FUN_OBJ_0(rtos_uptime_ms_obj, rtos_uptime_ms);

// rtos.cpu_freq() -> int (Hz)
static mp_obj_t rtos_cpu_freq(void) {
    uint32_t freq_hz;
    esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_APPROX, &freq_hz);
    return mp_obj_new_int(freq_hz);
}
static MP_DEFINE_CONST_FUN_OBJ_0(rtos_cpu_freq_obj, rtos_cpu_freq);

// ── Semaphore ───────────────────────────────────────────────────────────────

typedef struct {
    mp_obj_base_t base;
    SemaphoreHandle_t handle;
} rtos_semaphore_t;

static mp_obj_t semaphore_make_new(const mp_obj_type_t *type, size_t n_args,
                                    size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_count };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_count, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args,
                               MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    rtos_semaphore_t *self = mp_obj_malloc(rtos_semaphore_t, type);
    int count = args[ARG_count].u_int;
    if (count <= 1) {
        self->handle = xSemaphoreCreateBinary();
        xSemaphoreGive(self->handle);  // start as available
    } else {
        self->handle = xSemaphoreCreateCounting(count, count);
    }
    if (self->handle == NULL) {
        mp_raise_OSError(MP_ENOMEM);
    }
    return MP_OBJ_FROM_PTR(self);
}

// sem.acquire(timeout_ms=-1) -> bool
static mp_obj_t semaphore_acquire(size_t n_args, const mp_obj_t *pos_args,
                                   mp_map_t *kw_args) {
    enum { ARG_self, ARG_timeout_ms };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_,          MP_ARG_REQUIRED | MP_ARG_OBJ },
        { MP_QSTR_timeout_ms, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args,
                     MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    rtos_semaphore_t *self = MP_OBJ_TO_PTR(args[ARG_self].u_obj);
    int timeout = args[ARG_timeout_ms].u_int;
    TickType_t ticks = (timeout < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout);

    return mp_obj_new_bool(xSemaphoreTake(self->handle, ticks) == pdTRUE);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(semaphore_acquire_obj, 1, semaphore_acquire);

// sem.release()
static mp_obj_t semaphore_release(mp_obj_t self_in) {
    rtos_semaphore_t *self = MP_OBJ_TO_PTR(self_in);
    xSemaphoreGive(self->handle);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(semaphore_release_obj, semaphore_release);

static const mp_rom_map_elem_t semaphore_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_acquire), MP_ROM_PTR(&semaphore_acquire_obj) },
    { MP_ROM_QSTR(MP_QSTR_release), MP_ROM_PTR(&semaphore_release_obj) },
};
static MP_DEFINE_CONST_DICT(semaphore_locals, semaphore_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    rtos_semaphore_type,
    MP_QSTR_Semaphore,
    MP_TYPE_FLAG_NONE,
    make_new, semaphore_make_new,
    locals_dict, &semaphore_locals
);

// ── Queue ───────────────────────────────────────────────────────────────────

typedef struct {
    mp_obj_base_t base;
    QueueHandle_t handle;
    size_t item_size;
} rtos_queue_t;

static mp_obj_t queue_make_new(const mp_obj_type_t *type, size_t n_args,
                                size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_length, ARG_item_size };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_length,    MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 10} },
        { MP_QSTR_item_size, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 64} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args,
                               MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    rtos_queue_t *self = mp_obj_malloc(rtos_queue_t, type);
    self->item_size = args[ARG_item_size].u_int;
    self->handle = xQueueCreate(args[ARG_length].u_int, self->item_size);
    if (self->handle == NULL) {
        mp_raise_OSError(MP_ENOMEM);
    }
    return MP_OBJ_FROM_PTR(self);
}

// q.put(data_bytes, timeout_ms=-1) -> bool
static mp_obj_t queue_put(size_t n_args, const mp_obj_t *pos_args,
                           mp_map_t *kw_args) {
    enum { ARG_self, ARG_data, ARG_timeout_ms };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_,          MP_ARG_REQUIRED | MP_ARG_OBJ },
        { MP_QSTR_data,      MP_ARG_REQUIRED | MP_ARG_OBJ },
        { MP_QSTR_timeout_ms, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args,
                     MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    rtos_queue_t *self = MP_OBJ_TO_PTR(args[ARG_self].u_obj);
    mp_buffer_info_t buf;
    mp_get_buffer_raise(args[ARG_data].u_obj, &buf, MP_BUFFER_READ);

    // Pad or truncate to item_size
    char item[self->item_size];
    memset(item, 0, self->item_size);
    size_t copy_len = buf.len < self->item_size ? buf.len : self->item_size;
    memcpy(item, buf.buf, copy_len);

    int timeout = args[ARG_timeout_ms].u_int;
    TickType_t ticks = (timeout < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout);

    return mp_obj_new_bool(xQueueSend(self->handle, item, ticks) == pdTRUE);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(queue_put_obj, 2, queue_put);

// q.get(timeout_ms=-1) -> bytes or None
static mp_obj_t queue_get(size_t n_args, const mp_obj_t *pos_args,
                           mp_map_t *kw_args) {
    enum { ARG_self, ARG_timeout_ms };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_,          MP_ARG_REQUIRED | MP_ARG_OBJ },
        { MP_QSTR_timeout_ms, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args,
                     MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    rtos_queue_t *self = MP_OBJ_TO_PTR(args[ARG_self].u_obj);
    int timeout = args[ARG_timeout_ms].u_int;
    TickType_t ticks = (timeout < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout);

    char item[self->item_size];
    if (xQueueReceive(self->handle, item, ticks) == pdTRUE) {
        return mp_obj_new_bytes((const byte *)item, self->item_size);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(queue_get_obj, 1, queue_get);

// q.available() -> int
static mp_obj_t queue_available(mp_obj_t self_in) {
    rtos_queue_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(uxQueueMessagesWaiting(self->handle));
}
static MP_DEFINE_CONST_FUN_OBJ_1(queue_available_obj, queue_available);

static const mp_rom_map_elem_t queue_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_put),       MP_ROM_PTR(&queue_put_obj) },
    { MP_ROM_QSTR(MP_QSTR_get),       MP_ROM_PTR(&queue_get_obj) },
    { MP_ROM_QSTR(MP_QSTR_available), MP_ROM_PTR(&queue_available_obj) },
};
static MP_DEFINE_CONST_DICT(queue_locals, queue_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    rtos_queue_type,
    MP_QSTR_Queue,
    MP_TYPE_FLAG_NONE,
    make_new, queue_make_new,
    locals_dict, &queue_locals
);

// ── Event Group (multi-bit wait/signal) ─────────────────────────────────────

typedef struct {
    mp_obj_base_t base;
    EventGroupHandle_t handle;
} rtos_eventgroup_t;

static mp_obj_t eventgroup_make_new(const mp_obj_type_t *type, size_t n_args,
                                     size_t n_kw, const mp_obj_t *all_args) {
    mp_arg_check_num(n_args, n_kw, 0, 0, false);
    rtos_eventgroup_t *self = mp_obj_malloc(rtos_eventgroup_t, type);
    self->handle = xEventGroupCreate();
    if (self->handle == NULL) mp_raise_OSError(MP_ENOMEM);
    return MP_OBJ_FROM_PTR(self);
}

// eg.set(bits) -> previous bits
static mp_obj_t eventgroup_set(mp_obj_t self_in, mp_obj_t bits_in) {
    rtos_eventgroup_t *self = MP_OBJ_TO_PTR(self_in);
    EventBits_t prev = xEventGroupSetBits(self->handle, mp_obj_get_int(bits_in));
    return mp_obj_new_int(prev);
}
static MP_DEFINE_CONST_FUN_OBJ_2(eventgroup_set_obj, eventgroup_set);

// eg.clear(bits) -> previous bits
static mp_obj_t eventgroup_clear(mp_obj_t self_in, mp_obj_t bits_in) {
    rtos_eventgroup_t *self = MP_OBJ_TO_PTR(self_in);
    EventBits_t prev = xEventGroupClearBits(self->handle, mp_obj_get_int(bits_in));
    return mp_obj_new_int(prev);
}
static MP_DEFINE_CONST_FUN_OBJ_2(eventgroup_clear_obj, eventgroup_clear);

// eg.wait(bits, all=False, clear=True, timeout_ms=-1) -> bits
static mp_obj_t eventgroup_wait(size_t n_args, const mp_obj_t *pos_args,
                                 mp_map_t *kw_args) {
    enum { ARG_self, ARG_bits, ARG_all, ARG_clear, ARG_timeout_ms };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_,          MP_ARG_REQUIRED | MP_ARG_OBJ },
        { MP_QSTR_bits,      MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_all,       MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_clear,     MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = true} },
        { MP_QSTR_timeout_ms, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args,
                     MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    rtos_eventgroup_t *self = MP_OBJ_TO_PTR(args[ARG_self].u_obj);
    int timeout = args[ARG_timeout_ms].u_int;
    TickType_t ticks = (timeout < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout);

    EventBits_t result = xEventGroupWaitBits(
        self->handle,
        args[ARG_bits].u_int,
        args[ARG_clear].u_bool ? pdTRUE : pdFALSE,
        args[ARG_all].u_bool ? pdTRUE : pdFALSE,
        ticks
    );
    return mp_obj_new_int(result);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(eventgroup_wait_obj, 2, eventgroup_wait);

// eg.get() -> current bits
static mp_obj_t eventgroup_get(mp_obj_t self_in) {
    rtos_eventgroup_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(xEventGroupGetBits(self->handle));
}
static MP_DEFINE_CONST_FUN_OBJ_1(eventgroup_get_obj, eventgroup_get);

static const mp_rom_map_elem_t eventgroup_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_set),   MP_ROM_PTR(&eventgroup_set_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear), MP_ROM_PTR(&eventgroup_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_wait),  MP_ROM_PTR(&eventgroup_wait_obj) },
    { MP_ROM_QSTR(MP_QSTR_get),   MP_ROM_PTR(&eventgroup_get_obj) },
};
static MP_DEFINE_CONST_DICT(eventgroup_locals, eventgroup_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    rtos_eventgroup_type,
    MP_QSTR_EventGroup,
    MP_TYPE_FLAG_NONE,
    make_new, eventgroup_make_new,
    locals_dict, &eventgroup_locals
);

// ── Task Watchdog (TWDT) ────────────────────────────────────────────────────

// rtos.wdt_init(timeout_ms=5000)
static mp_obj_t rtos_wdt_init(size_t n_args, const mp_obj_t *args) {
    int timeout_ms = (n_args > 0) ? mp_obj_get_int(args[0]) : 5000;
    esp_task_wdt_config_t config = {
        .timeout_ms = timeout_ms,
        .idle_core_mask = 0,  // don't watch idle tasks
        .trigger_panic = false,
    };
    esp_err_t err = esp_task_wdt_reconfigure(&config);
    if (err == ESP_ERR_NOT_FOUND) {
        err = esp_task_wdt_init(&config);
    }
    if (err == ESP_OK) {
        esp_task_wdt_add(NULL);  // add current task
    }
    return mp_obj_new_bool(err == ESP_OK);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rtos_wdt_init_obj, 0, 1, rtos_wdt_init);

// rtos.wdt_feed()
static mp_obj_t rtos_wdt_feed(void) {
    esp_task_wdt_reset();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(rtos_wdt_feed_obj, rtos_wdt_feed);

// rtos.wdt_stop()
static mp_obj_t rtos_wdt_stop(void) {
    esp_task_wdt_delete(NULL);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(rtos_wdt_stop_obj, rtos_wdt_stop);

// ── Power Management ────────────────────────────────────────────────────────

// rtos.lightsleep(ms) — light sleep with timer wakeup
static mp_obj_t rtos_lightsleep(mp_obj_t ms_in) {
    int ms = mp_obj_get_int(ms_in);
    esp_sleep_enable_timer_wakeup(ms * 1000ULL);
    esp_light_sleep_start();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(rtos_lightsleep_obj, rtos_lightsleep);

// rtos.deepsleep(ms) — deep sleep (full reset on wake)
static mp_obj_t rtos_deepsleep(mp_obj_t ms_in) {
    int ms = mp_obj_get_int(ms_in);
    esp_sleep_enable_timer_wakeup(ms * 1000ULL);
    esp_deep_sleep_start();
    return mp_const_none;  // never reached
}
static MP_DEFINE_CONST_FUN_OBJ_1(rtos_deepsleep_obj, rtos_deepsleep);

// rtos.wake_reason() -> str
static mp_obj_t rtos_wake_reason(void) {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    const char *reason;
    switch (cause) {
        case ESP_SLEEP_WAKEUP_TIMER: reason = "timer"; break;
        case ESP_SLEEP_WAKEUP_GPIO: reason = "gpio"; break;
        case ESP_SLEEP_WAKEUP_TOUCHPAD: reason = "touch"; break;
        case ESP_SLEEP_WAKEUP_EXT0: reason = "ext0"; break;
        case ESP_SLEEP_WAKEUP_EXT1: reason = "ext1"; break;
        case ESP_SLEEP_WAKEUP_ULP: reason = "ulp"; break;
        default: reason = "power_on"; break;
    }
    return mp_obj_new_str(reason, strlen(reason));
}
static MP_DEFINE_CONST_FUN_OBJ_0(rtos_wake_reason_obj, rtos_wake_reason);

// rtos.freq(mhz) — set CPU frequency (80, 160, 240)
static mp_obj_t rtos_set_freq(mp_obj_t mhz_in) {
    int mhz = mp_obj_get_int(mhz_in);
    esp_pm_config_t pm_config = {
        .max_freq_mhz = mhz,
        .min_freq_mhz = mhz,
        .light_sleep_enable = false,
    };
    esp_err_t err = esp_pm_configure(&pm_config);
    if (err != ESP_OK) {
        mp_raise_msg_varg(&mp_type_ValueError,
                          MP_ERROR_TEXT("Invalid freq %d MHz"), mhz);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(rtos_set_freq_obj, rtos_set_freq);

// ── High-resolution timer ───────────────────────────────────────────────────

// rtos.ticks_us() -> int (microsecond precision, no overflow for ~72 min)
static mp_obj_t rtos_ticks_us(void) {
    return mp_obj_new_int_from_ull(esp_timer_get_time());
}
static MP_DEFINE_CONST_FUN_OBJ_0(rtos_ticks_us_obj, rtos_ticks_us);

// ── CPU usage per task ──────────────────────────────────────────────────────

#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
static mp_obj_t rtos_cpu_usage(void) {
    UBaseType_t count = uxTaskGetNumberOfTasks();
    TaskStatus_t *task_array = m_malloc(count * sizeof(TaskStatus_t));
    uint32_t total_runtime;

    UBaseType_t got = uxTaskGetSystemState(task_array, count, &total_runtime);

    mp_obj_dict_t *result = MP_OBJ_TO_PTR(mp_obj_new_dict(got));

    if (total_runtime > 0) {
        for (UBaseType_t i = 0; i < got; i++) {
            uint32_t pct = (task_array[i].ulRunTimeCounter * 100) / total_runtime;
            mp_obj_dict_store(result,
                              mp_obj_new_str(task_array[i].pcTaskName,
                                             strlen(task_array[i].pcTaskName)),
                              mp_obj_new_int(pct));
        }
    }

    m_free(task_array);
    return MP_OBJ_FROM_PTR(result);
}
static MP_DEFINE_CONST_FUN_OBJ_0(rtos_cpu_usage_obj, rtos_cpu_usage);
#endif

// ── Module definition ───────────────────────────────────────────────────────

static const mp_rom_map_elem_t warpig_rtos_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),    MP_ROM_QSTR(MP_QSTR_warpig_rtos) },

    // Functions
    { MP_ROM_QSTR(MP_QSTR_tasks),       MP_ROM_PTR(&rtos_tasks_obj) },
    { MP_ROM_QSTR(MP_QSTR_task_count),  MP_ROM_PTR(&rtos_task_count_obj) },
    { MP_ROM_QSTR(MP_QSTR_free_heap),   MP_ROM_PTR(&rtos_free_heap_obj) },
    { MP_ROM_QSTR(MP_QSTR_uptime_ms),   MP_ROM_PTR(&rtos_uptime_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_cpu_freq),    MP_ROM_PTR(&rtos_cpu_freq_obj) },
    { MP_ROM_QSTR(MP_QSTR_pin_to_core), MP_ROM_PTR(&rtos_pin_to_core_obj) },

    // Watchdog
    { MP_ROM_QSTR(MP_QSTR_wdt_init),    MP_ROM_PTR(&rtos_wdt_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_wdt_feed),    MP_ROM_PTR(&rtos_wdt_feed_obj) },
    { MP_ROM_QSTR(MP_QSTR_wdt_stop),    MP_ROM_PTR(&rtos_wdt_stop_obj) },

    // Power management
    { MP_ROM_QSTR(MP_QSTR_lightsleep),  MP_ROM_PTR(&rtos_lightsleep_obj) },
    { MP_ROM_QSTR(MP_QSTR_deepsleep),   MP_ROM_PTR(&rtos_deepsleep_obj) },
    { MP_ROM_QSTR(MP_QSTR_wake_reason), MP_ROM_PTR(&rtos_wake_reason_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_freq),    MP_ROM_PTR(&rtos_set_freq_obj) },

    // Timing
    { MP_ROM_QSTR(MP_QSTR_ticks_us),    MP_ROM_PTR(&rtos_ticks_us_obj) },

    // CPU usage
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    { MP_ROM_QSTR(MP_QSTR_cpu_usage),   MP_ROM_PTR(&rtos_cpu_usage_obj) },
#endif

    // Classes
    { MP_ROM_QSTR(MP_QSTR_Semaphore),   MP_ROM_PTR(&rtos_semaphore_type) },
    { MP_ROM_QSTR(MP_QSTR_Queue),       MP_ROM_PTR(&rtos_queue_type) },
    { MP_ROM_QSTR(MP_QSTR_EventGroup),  MP_ROM_PTR(&rtos_eventgroup_type) },
};
static MP_DEFINE_CONST_DICT(warpig_rtos_globals, warpig_rtos_globals_table);

const mp_obj_module_t warpig_rtos_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&warpig_rtos_globals,
};

MP_REGISTER_MODULE(MP_QSTR_warpig_rtos, warpig_rtos_module);
