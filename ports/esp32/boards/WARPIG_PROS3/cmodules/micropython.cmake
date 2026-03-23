# WarpPig C modules
include(${CMAKE_CURRENT_LIST_DIR}/warpig_http/micropython.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/warpig_httpd/micropython.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/warpig_rtos/micropython.cmake)

# ulab — numpy/scipy for MicroPython (FFT, linalg, arrays)
include(/opt/micropython_build/ulab/code/micropython.cmake)
