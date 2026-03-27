/*
 * WarpPig Nesso — Arduino Nesso N1 (ESP32-C6 + M5Stack collab)
 *
 * Hardware:
 *   - ESP32-C6 RISC-V single-core @ 160MHz
 *   - 16MB SPI flash, 512KB SRAM (no PSRAM)
 *   - 1.14" IPS display 135x240 (ST7789P3, SPI via I/O expander)
 *   - Capacitive touch controller
 *   - BMI270 6-axis IMU (I2C, SDA=19, SCL=20, INT=3)
 *   - SX1262 LoRa 850-960MHz (SPI via I/O expander)
 *   - BQ27220 battery fuel gauge (I2C)
 *   - PCA9554 + PCA9539 I/O expanders (I2C)
 *   - WiFi 6, BLE 5.3, Thread/Zigbee, LoRa
 *   - IR transmitter (GPIO10), buzzer (GPIO11)
 *   - 250mAh LiPo, USB-C
 *   - Grove (GPIO4/5), Qwiic (GPIO18/8), M5StickC HAT (8-pin)
 *
 * Pin map (direct ESP32-C6 GPIOs):
 *   IMU I2C:     SDA=19, SCL=20, INT=3
 *   Grove:       GPIO4 (SDA/ADC), GPIO5 (SCL/ADC)
 *   Qwiic:       SDA=18, SCL=8
 *   IR TX:       GPIO10
 *   Buzzer:      GPIO11
 *   User button: GPIO7
 *   Power btn:   GPIO9
 *   USB:         DP=13, DN=12
 *   HAT:         GPIO6, +5V, +3V3, BATTERY
 *
 * Note: Display, LED, LoRa controlled via I2C I/O expanders (PCA9554/PCA9539)
 */

#define MICROPY_HW_BOARD_NAME               "WarpPig Nesso (IoT+CSI+LoRa)"
#define MICROPY_HW_MCU_NAME                 "ESP32C6"
#define MICROPY_PY_NETWORK_HOSTNAME_DEFAULT "warpig-nesso"

// ── I2C bus 0 (IMU + I/O expanders + fuel gauge) ───────────────────────
#define MICROPY_HW_I2C0_SCL                 (20)
#define MICROPY_HW_I2C0_SDA                 (19)

// ── I2C bus 1 (Qwiic connector) ────────────────────────────────────────
#define MICROPY_HW_I2C1_SCL                 (8)
#define MICROPY_HW_I2C1_SDA                 (18)

// ── WiFi CSI support (ESP32-C6 has modern CSI API) ─────────────────────
#define MICROPY_PY_NETWORK_WLAN_CSI         (1)
#define MICROPY_PY_NETWORK_WLAN_CSI_DEFAULT_BUFFER_SIZE (16)

// ── Threading ───────────────────────────────────────────────────────────
#define MICROPY_PY_THREAD                   (1)
#define MICROPY_PY_THREAD_GIL               (1)

// ── Performance ─────────────────────────────────────────────────────────
#define MICROPY_OPT_COMPUTED_GOTO           (1)
#define MICROPY_OPT_CACHE_MAP_LOOKUP_IN_BYTECODE (1)

// ── UART REPL on USB-JTAG ──────────────────────────────────────────────
#define MICROPY_HW_ENABLE_UART_REPL         (1)
