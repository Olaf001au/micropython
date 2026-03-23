add_library(warpig_http INTERFACE)

target_sources(warpig_http INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/mod_warpig_http.c
)

target_include_directories(warpig_http INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
    $ENV{IDF_PATH}/components/mbedtls/esp_crt_bundle/include
    $ENV{IDF_PATH}/components/esp_http_client/include
    $ENV{IDF_PATH}/components/esp-tls
    $ENV{IDF_PATH}/components/esp-tls/esp-tls-crypto
    $ENV{IDF_PATH}/components/tcp_transport/include
)

target_link_libraries(usermod INTERFACE warpig_http)
