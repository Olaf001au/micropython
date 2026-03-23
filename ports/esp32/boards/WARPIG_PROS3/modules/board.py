"""
WarpPig ProS3 board helper — frozen into firmware.

Based on Unexpected Maker ProS3 hardware:
  - ESP32-S3 dual-core LX7 @ 240MHz
  - 16MB flash, 8MB OPI PSRAM
  - WS2812B NeoPixel on GPIO18 (LDO2 powered)
  - LiPo battery sense on GPIO10
  - USB VBUS detect on GPIO33

Usage:
    import board
    board.neopixel(0xff0000)      # red
    board.neopixel_off()
    print(board.battery_voltage())  # 3.85
    print(board.usb_connected())    # True
    print(board.info())             # dict with all hw info
"""

from micropython import const
from machine import Pin, ADC
import neopixel as _neopixel_mod
import gc
import os

# ── Pin assignments (from ProS3 pinout diagram) ─────────────────────────────

# Power
VBUS_SENSE = const(33)
VBAT_SENSE = const(10)
LDO2       = const(17)

# NeoPixel
RGB_DATA   = const(18)

# I2C
I2C_SDA    = const(8)
I2C_SCL    = const(9)

# SPI
SPI_MOSI   = const(35)
SPI_MISO   = const(37)
SPI_SCK    = const(36)
SPI_SS     = const(34)

# UART
UART_TX    = const(43)
UART_RX    = const(44)

# ADC1 pins (safe with WiFi)
ADC_PINS   = (1, 2, 3, 4, 5, 6, 7)

# Touch pins
TOUCH_PINS = (1, 2, 3, 4, 5, 6, 7, 8, 9)

# ── NeoPixel ─────────────────────────────────────────────────────────────────

_pixel = None

def _init_pixel():
    global _pixel
    if _pixel is None:
        Pin(LDO2, Pin.OUT).value(1)  # power on LDO2
        _pixel = _neopixel_mod.NeoPixel(Pin(RGB_DATA), 1)
    return _pixel


def neopixel(color, brightness=0.3):
    """Set NeoPixel color. color can be int (0xRRGGBB) or (r,g,b) tuple."""
    p = _init_pixel()
    if isinstance(color, int):
        r = (color >> 16) & 0xff
        g = (color >> 8) & 0xff
        b = color & 0xff
    else:
        r, g, b = color
    r = int(r * brightness)
    g = int(g * brightness)
    b = int(b * brightness)
    p[0] = (r, g, b)
    p.write()


def neopixel_off():
    """Turn off NeoPixel and LDO2 to save power."""
    global _pixel
    if _pixel:
        _pixel[0] = (0, 0, 0)
        _pixel.write()
    Pin(LDO2, Pin.OUT).value(0)
    _pixel = None


def color_wheel(pos):
    """Rainbow color wheel. pos 0-255 -> (r, g, b)."""
    pos = pos % 255
    if pos < 85:
        return 255 - pos * 3, 0, pos * 3
    elif pos < 170:
        pos -= 85
        return 0, pos * 3, 255 - pos * 3
    else:
        pos -= 170
        return pos * 3, 255 - pos * 3, 0


# ── Power sensing ────────────────────────────────────────────────────────────

def battery_voltage():
    """Read battery voltage (1S LiPo, max 4.2V). Returns float."""
    adc = ADC(Pin(VBAT_SENSE))
    adc.atten(ADC.ATTN_11DB)
    # Discard first reads for ADC warmup
    for _ in range(10):
        adc.read()
    raw = 0
    for _ in range(4):
        raw += adc.read()
    raw //= 4
    # ProS3 has 1:1 voltage divider, ADC range 0-3.3V with 11dB attenuation
    return round(raw / 4095 * 4.2, 2)


def battery_percent():
    """Estimate battery percentage (very rough — LiPo discharge curve)."""
    v = battery_voltage()
    if v >= 4.1:
        return 100
    elif v <= 3.0:
        return 0
    else:
        return int((v - 3.0) / 1.1 * 100)


def usb_connected():
    """Returns True if USB VBUS (5V) is present."""
    return Pin(VBUS_SENSE, Pin.IN).value() == 1


def set_ldo2(state):
    """Enable/disable LDO2 (powers NeoPixel + external QWIIC/STEMMA)."""
    Pin(LDO2, Pin.OUT).value(1 if state else 0)


# ── System info ──────────────────────────────────────────────────────────────

def info():
    """Return dict with board hardware info."""
    gc.collect()
    flash = os.statvfs("/")

    d = {
        "board": "WarpPig ProS3",
        "mcu": "ESP32-S3",
        "flash_total": flash[0] * flash[2],
        "flash_free": flash[0] * flash[3],
        "ram_free": gc.mem_free(),
        "ram_alloc": gc.mem_alloc(),
        "battery_v": battery_voltage(),
        "usb": usb_connected(),
    }

    # Add PSRAM info if available
    try:
        import esp32
        d["cpu_temp_c"] = esp32.mcu_temperature()
    except Exception:
        pass

    try:
        import warpig_rtos
        heap = warpig_rtos.free_heap()
        d["psram_free"] = heap.get("psram", 0)
        d["internal_free"] = heap.get("internal", 0)
        d["cpu_freq"] = warpig_rtos.cpu_freq()
    except ImportError:
        pass

    return d
