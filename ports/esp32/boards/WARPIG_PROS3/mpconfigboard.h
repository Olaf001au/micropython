/*
 * WarpPig ProS3 — Custom board config for Unexpected Maker ProS3 (ESP32-S3)
 *
 * Hardware:
 *   - ESP32-S3 (QFN56), dual-core Xtensa LX7 @ 240MHz
 *   - 16MB QSPI Flash (QIO 80MHz)
 *   - 8MB OPI PSRAM
 *   - USB-OTG (native USB, no UART bridge)
 *   - 1x WS2812B NeoPixel on GPIO18 (powered by LDO2 on GPIO17)
 *   - LiPo battery: VBAT sense on GPIO10 (ADC, 1S max 4.2V)
 *   - VBUS (5V USB) detect on GPIO33
 *   - WiFi + BLE 5.0
 *
 * Pin groups from pinout diagram:
 *   I2C:  SDA=GPIO8, SCL=GPIO9
 *   SPI:  MOSI=GPIO35, MISO=GPIO37, SCK=GPIO36, SS=GPIO34
 *   UART: TX=GPIO43, RX=GPIO44
 *   ADC1: GPIO1-7 (safe with WiFi)
 *   Touch: GPIO1-9
 *   JTAG: GPIO39-42
 *   Strapping: GPIO0, GPIO3 (boot mode)
 */

#define MICROPY_HW_BOARD_NAME               "WarpPig ProS3 (AI+RTOS)"
#define MICROPY_HW_MCU_NAME                 "ESP32-S3"
#define MICROPY_PY_NETWORK_HOSTNAME_DEFAULT "warpig-ai"

// ── Float64 ─────────────────────────────────────────────────────────────────
#ifndef MICROPY_FLOAT_IMPL
#define MICROPY_FLOAT_IMPL                  MICROPY_FLOAT_IMPL_DOUBLE
#endif

// ── I2C bus 0 (Qwiic/STEMMA QT connector) ──────────────────────────────────
#define MICROPY_HW_I2C0_SCL                 (9)
#define MICROPY_HW_I2C0_SDA                 (8)

// ── SPI bus 1 ───────────────────────────────────────────────────────────────
#define MICROPY_HW_SPI1_MOSI                (35)
#define MICROPY_HW_SPI1_MISO                (37)
#define MICROPY_HW_SPI1_SCK                 (36)

// ── USB (native USB-OTG, no CP210x) ────────────────────────────────────────
#define MICROPY_HW_USB_VID                  0x303A
#define MICROPY_HW_USB_PID                  0x80D4
#define MICROPY_HW_USB_MANUFACTURER_STRING  "WarpPig"
#define MICROPY_HW_USB_PRODUCT_FS_STRING    "WarpPig AI ProS3"

// ── ProS3 hardware constants (baked into firmware) ──────────────────────────
// These match pros3.py helper lib — available as machine constants
#define MICROPY_HW_NEOPIXEL_PIN             (18)
#define MICROPY_HW_NEOPIXEL_COUNT           (1)
#define MICROPY_HW_LDO2_PIN                 (17)
#define MICROPY_HW_VBUS_SENSE_PIN           (33)
#define MICROPY_HW_VBAT_SENSE_PIN           (10)

// ── Threading (real FreeRTOS tasks, dual-core) ──────────────────────────────
#define MICROPY_PY_THREAD                   (1)
#define MICROPY_PY_THREAD_GIL               (1)
#define MICROPY_PY_THREAD_GIL_VM_DIVISOR    (64)  // less GIL contention (default 32)

// ── Memory ──────────────────────────────────────────────────────────────────
#ifndef MICROPY_ALLOC_GC_STACK_SIZE
#define MICROPY_ALLOC_GC_STACK_SIZE         (2048)
#endif

// ── Performance: Native Code Emitter ────────────────────────────────────────
// @micropython.native — compile Python to Xtensa machine code (~2-10x faster)
// @micropython.viper  — pointer-based, near-C speed for tight loops
// Already enabled via MICROPY_EMIT_XTENSAWIN in mpconfigport.h

// ── Performance: Compiler Optimizations ─────────────────────────────────────
#define MICROPY_OPT_COMPUTED_GOTO           (1)   // threaded dispatch (~20% faster bytecode)
#define MICROPY_OPT_CACHE_MAP_LOOKUP_IN_BYTECODE (1) // cache dict lookups in bytecode
#define MICROPY_OPT_MPZ_BITWISE             (1)   // fast bignum bitwise ops
#define MICROPY_OPT_MATH_FACTORIAL          (1)   // optimized math.factorial

// ── Performance: Startup ────────────────────────────────────────────────────
#define MICROPY_MODULE_FROZEN_STR           (0)   // don't freeze .py as strings (use .mpy)

// ── Enable UART REPL on USB-OTG ────────────────────────────────────────────
#define MICROPY_HW_ENABLE_UART_REPL         (0)  // USB CDC, not UART

// ── Profiling: sys.settrace support ─────────────────────────────────────────
#define MICROPY_PY_SYS_SETTRACE             (1)   // enable function-level tracing
#define MICROPY_COMP_CONST_FOLDING          (1)   // fold constants at compile time
#define MICROPY_COMP_CONST_LITERAL          (1)   // optimize literal constants
