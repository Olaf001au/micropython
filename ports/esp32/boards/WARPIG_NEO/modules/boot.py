import gc
import sys
import os

BANNER = """
\033[38;5;208m
  ██╗    ██╗ █████╗ ██████╗ ██████╗ ██╗ ██████╗
  ██║    ██║██╔══██╗██╔══██╗██╔══██╗██║██╔════╝
  ██║ █╗ ██║███████║██████╔╝██████╔╝██║██║  ███╗
  ██║███╗██║██╔══██║██╔══██╗██╔═══╝ ██║██║   ██║
  ╚███╔███╔╝██║  ██║██║  ██║██║     ██║╚██████╔╝
   ╚══╝╚══╝ ╚═╝  ╚═╝╚═╝  ╚═╝╚═╝     ╚═╝ ╚═════╝
\033[0m\033[38;5;245m
  MicroPython {ver} | ESP32-S3 @ 240MHz | float64
  Board: {board}
  Flash: {flash}KB free | RAM: {ram}KB free
  Modules: warpig_http warpig_httpd warpig_rtos ulab
  Native: @native @viper | Cores: 2 | RTOS: FreeRTOS
\033[38;5;70m
  Ready.\033[0m
"""

gc.collect()
uname = os.uname()
flash = os.statvfs('/')
flash_free = (flash[0] * flash[3]) // 1024
ram_free = gc.mem_free() // 1024

print(BANNER.format(
    ver='.'.join(str(x) for x in sys.implementation.version[:3]),
    board=uname.machine.split(' with ')[0],
    flash=flash_free,
    ram=ram_free,
))
