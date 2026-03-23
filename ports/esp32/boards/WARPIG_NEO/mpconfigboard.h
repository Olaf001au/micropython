/*
 * WarpPig Neo — FeatherS3 Neo (ESP32-S3) custom board config
 *
 * Hardware:
 *   - ESP32-S3 @ 240MHz, 8MB flash, 2MB PSRAM
 *   - WS2812B NeoPixel on GPIO40 (powered by LDO2/GPIO39)
 *   - 7x7 RGB LED matrix on GPIO16 (also powered by GPIO39)
 *   - Blue status LED on GPIO13
 *   - Ambient light sensor on GPIO4 (ADC)
 *   - LiPo battery: VBAT on GPIO2, VBUS on GPIO15
 *   - Feather form factor with STEMMA QT/Qwiic
 */

#define MICROPY_HW_BOARD_NAME               "WarpPig Neo (AI+RTOS)"
#define MICROPY_HW_MCU_NAME                 "ESP32-S3"
#define MICROPY_PY_NETWORK_HOSTNAME_DEFAULT "warpig-neo"

#ifndef MICROPY_FLOAT_IMPL
#define MICROPY_FLOAT_IMPL                  MICROPY_FLOAT_IMPL_DOUBLE
#endif

#define MICROPY_HW_I2C0_SCL                 (9)
#define MICROPY_HW_I2C0_SDA                 (8)

#define MICROPY_HW_SPI1_MOSI                (35)
#define MICROPY_HW_SPI1_MISO                (37)
#define MICROPY_HW_SPI1_SCK                 (36)

#define MICROPY_HW_USB_VID                  0x303A
#define MICROPY_HW_USB_PID                  0x81FC
#define MICROPY_HW_USB_MANUFACTURER_STRING  "WarpPig"
#define MICROPY_HW_USB_PRODUCT_FS_STRING    "WarpPig Neo"

// FeatherS3 Neo specific hardware
#define MICROPY_HW_NEOPIXEL_PIN             (40)
#define MICROPY_HW_LED_MATRIX_PIN           (16)
#define MICROPY_HW_LDO2_PIN                 (39)
#define MICROPY_HW_LED_BLUE_PIN             (13)
#define MICROPY_HW_VBUS_SENSE_PIN           (15)
#define MICROPY_HW_VBAT_SENSE_PIN           (2)
#define MICROPY_HW_AMB_LIGHT_PIN            (4)

#define MICROPY_PY_THREAD                   (1)
#define MICROPY_PY_THREAD_GIL               (1)
// GIL_VM_DIVISOR set in mpconfigport.h (default 32)

#ifndef MICROPY_ALLOC_GC_STACK_SIZE
#define MICROPY_ALLOC_GC_STACK_SIZE         (2048)
#endif

#define MICROPY_OPT_COMPUTED_GOTO           (1)
#define MICROPY_OPT_CACHE_MAP_LOOKUP_IN_BYTECODE (1)
#define MICROPY_PY_SYS_SETTRACE             (1)
#define MICROPY_COMP_CONST_FOLDING          (1)

// UART REPL on USB-Serial/JTAG (Neo uses this, not USB-OTG)
#define MICROPY_HW_ENABLE_UART_REPL         (1)
