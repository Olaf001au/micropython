/*
 * WarpPig Puck — Elecrow CrowPanel 1.28" Rotary (ESP32-S3)
 *
 * Hardware:
 *   - ESP32-S3R8 @ 240MHz, 16MB flash, 8MB OPI PSRAM
 *   - 1.28" round IPS 240x240, GC9A01 (SPI2 @ 80MHz)
 *   - CST816D capacitive touch (I2C 0x15)
 *   - Rotary encoder (A=45, B=42, Switch=41)
 *   - WS2812B NeoPixel atmosphere LED on GPIO48
 *   - WiFi + BLE 5.0
 *
 * Pin map:
 *   Display SPI: SCLK=10, MOSI=11, CS=9, DC=3, RST=14, BL=46
 *   Touch I2C:   SDA=6, SCL=7
 *   User I2C:    SDA=38, SCL=39
 *   Encoder:     A=45, B=42, SW=41
 *   LED:         NeoPixel=48, Power=40
 */

#define MICROPY_HW_BOARD_NAME               "WarpPig Puck (CSI+Display)"
#define MICROPY_HW_MCU_NAME                 "ESP32-S3"
#define MICROPY_PY_NETWORK_HOSTNAME_DEFAULT "warpig-puck"

// ── Float64 ─────────────────────────────────────────────────────────────────
#ifndef MICROPY_FLOAT_IMPL
#define MICROPY_FLOAT_IMPL                  MICROPY_FLOAT_IMPL_DOUBLE
#endif

// ── Display SPI (GC9A01 240x240 round) ─────────────────────────────────────
#define MICROPY_HW_SPI2_SCK                 (10)
#define MICROPY_HW_SPI2_MOSI                (11)
#define MICROPY_HW_SPI2_MISO                (-1)   // not used

// ── Touch I2C (CST816D @ 0x15) ─────────────────────────────────────────────
#define MICROPY_HW_I2C0_SCL                 (7)
#define MICROPY_HW_I2C0_SDA                 (6)

// ── User I2C (STEMMA QT / expansion) ───────────────────────────────────────
#define MICROPY_HW_I2C1_SCL                 (39)
#define MICROPY_HW_I2C1_SDA                 (38)

// ── Display control pins ────────────────────────────────────────────────────
#define MICROPY_HW_DISPLAY_CS               (9)
#define MICROPY_HW_DISPLAY_DC               (3)
#define MICROPY_HW_DISPLAY_RST              (14)
#define MICROPY_HW_DISPLAY_BL               (46)

// ── Rotary encoder ──────────────────────────────────────────────────────────
#define MICROPY_HW_ENCODER_A                (45)
#define MICROPY_HW_ENCODER_B                (42)
#define MICROPY_HW_ENCODER_SW               (41)

// ── LED ─────────────────────────────────────────────────────────────────────
#define MICROPY_HW_NEOPIXEL_PIN             (48)
#define MICROPY_HW_NEOPIXEL_COUNT           (1)
#define MICROPY_HW_POWER_LIGHT_PIN          (40)

// ── USB (native USB-OTG) ────────────────────────────────────────────────────
#define MICROPY_HW_USB_VID                  0x303A
#define MICROPY_HW_USB_PID                  0x8200
#define MICROPY_HW_USB_MANUFACTURER_STRING  "WarpPig"
#define MICROPY_HW_USB_PRODUCT_FS_STRING    "WarpPig Puck"

// ── Threading ───────────────────────────────────────────────────────────────
#define MICROPY_PY_THREAD                   (1)
#define MICROPY_PY_THREAD_GIL               (1)

// ── Memory ──────────────────────────────────────────────────────────────────
#ifndef MICROPY_ALLOC_GC_STACK_SIZE
#define MICROPY_ALLOC_GC_STACK_SIZE         (2048)
#endif

// ── Performance ─────────────────────────────────────────────────────────────
#define MICROPY_OPT_COMPUTED_GOTO           (1)
#define MICROPY_OPT_CACHE_MAP_LOOKUP_IN_BYTECODE (1)

// ── WiFi CSI (Channel State Information) for human detection ────────────────
#define MICROPY_PY_NETWORK_WLAN_CSI         (1)
#define MICROPY_PY_NETWORK_WLAN_CSI_DEFAULT_BUFFER_SIZE (8)
