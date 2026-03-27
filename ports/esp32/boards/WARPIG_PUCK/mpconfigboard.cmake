set(IDF_TARGET esp32s3)

set(SDKCONFIG_DEFAULTS
    boards/sdkconfig.base
    boards/sdkconfig.ble
    boards/sdkconfig.240mhz
    boards/sdkconfig.spiram_oct   # 8MB OPI PSRAM
    boards/sdkconfig.csi
    boards/WARPIG_PUCK/sdkconfig.board
)

# WarpPig C modules (shared with ProS3)
set(USER_C_MODULES
    ${MICROPY_BOARD_DIR}/../WARPIG_PROS3/cmodules/micropython.cmake
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
