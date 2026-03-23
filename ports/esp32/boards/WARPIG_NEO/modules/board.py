"""
WarpPig Neo board helper — FeatherS3 Neo hardware.

Different from ProS3:
  - RGB LED on GPIO40 (powered by LDO2/GPIO39)
  - 7x7 RGB LED matrix on GPIO16 (same LDO2 power)
  - Blue status LED on GPIO13
  - Ambient light sensor on GPIO4
  - VBAT on GPIO2, VBUS on GPIO15
  - 8MB flash, 2MB PSRAM (vs ProS3's 16MB/8MB)
"""

from micropython import const
from machine import Pin, ADC
import neopixel as _neopixel_mod
import gc
import os

VBUS_SENSE      = const(15)
VBAT_SENSE      = const(2)
LDO2            = const(39)
RGB_DATA        = const(40)
RGB_MATRIX_DATA = const(16)
LED_BLUE        = const(13)
AMB_LIGHT       = const(4)
I2C_SDA         = const(8)
I2C_SCL         = const(9)
SPI_MOSI        = const(35)
SPI_MISO        = const(37)
SPI_SCK         = const(36)

_pixel = None
_blue = None

def _init_pixel():
    global _pixel
    if _pixel is None:
        Pin(LDO2, Pin.OUT).value(1)
        _pixel = _neopixel_mod.NeoPixel(Pin(RGB_DATA), 1)
    return _pixel

def neopixel(color, brightness=0.3):
    p = _init_pixel()
    if isinstance(color, int):
        r = (color >> 16) & 0xff
        g = (color >> 8) & 0xff
        b = color & 0xff
    else:
        r, g, b = color
    p[0] = (int(r * brightness), int(g * brightness), int(b * brightness))
    p.write()

def neopixel_off():
    global _pixel
    if _pixel:
        _pixel[0] = (0, 0, 0)
        _pixel.write()
    Pin(LDO2, Pin.OUT).value(0)
    _pixel = None

def blue_led(state):
    global _blue
    if _blue is None:
        _blue = Pin(LED_BLUE, Pin.OUT)
    _blue.value(1 if state else 0)

def ambient_light():
    adc = ADC(Pin(AMB_LIGHT))
    adc.atten(ADC.ATTN_11DB)
    return adc.read()

def battery_voltage():
    adc = ADC(Pin(VBAT_SENSE))
    adc.atten(ADC.ATTN_11DB)
    for _ in range(10):
        adc.read()
    raw = sum(adc.read() for _ in range(4)) // 4
    return round(raw / 4095 * 4.2, 2)

def battery_percent():
    v = battery_voltage()
    if v >= 4.1: return 100
    if v <= 3.0: return 0
    return int((v - 3.0) / 1.1 * 100)

def usb_connected():
    return Pin(VBUS_SENSE, Pin.IN).value() == 1

def set_ldo2(state):
    Pin(LDO2, Pin.OUT).value(1 if state else 0)

def color_wheel(pos):
    pos = pos % 255
    if pos < 85:
        return 255 - pos * 3, 0, pos * 3
    elif pos < 170:
        pos -= 85
        return 0, pos * 3, 255 - pos * 3
    else:
        pos -= 170
        return pos * 3, 255 - pos * 3, 0

def info():
    gc.collect()
    flash = os.statvfs("/")
    d = {
        "board": "WarpPig Neo",
        "mcu": "ESP32-S3",
        "flash_total": flash[0] * flash[2],
        "flash_free": flash[0] * flash[3],
        "ram_free": gc.mem_free(),
        "ram_alloc": gc.mem_alloc(),
        "battery_v": battery_voltage(),
        "usb": usb_connected(),
        "light": ambient_light(),
    }
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
