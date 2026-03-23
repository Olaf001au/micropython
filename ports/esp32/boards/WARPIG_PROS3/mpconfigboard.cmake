set(IDF_TARGET esp32s3)

set(SDKCONFIG_DEFAULTS
    boards/sdkconfig.base
    boards/sdkconfig.ble
    boards/sdkconfig.240mhz
    boards/sdkconfig.spiram_sx
    boards/WARPIG_PROS3/sdkconfig.board
)

# WarpPig C modules — native RTOS, HTTP, and HTTPD
set(USER_C_MODULES
    ${MICROPY_BOARD_DIR}/cmodules/micropython.cmake
)

# Extra ESP-IDF components for WarpPig C modules
list(APPEND IDF_COMPONENTS
    esp_http_client
    esp_http_server
    esp-tls
    tcp_transport
    http_parser
)

set(MICROPY_FROZEN_MANIFEST ${MICROPY_BOARD_DIR}/manifest.py)
