add_library(warpig_httpd INTERFACE)

target_sources(warpig_httpd INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/mod_warpig_httpd.c
)

target_include_directories(warpig_httpd INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
    $ENV{IDF_PATH}/components/esp_http_server/include
    $ENV{IDF_PATH}/components/http_parser
)

target_link_libraries(usermod INTERFACE warpig_httpd)
