add_library(warpig_rtos INTERFACE)

target_sources(warpig_rtos INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/mod_warpig_rtos.c
)

target_include_directories(warpig_rtos INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(usermod INTERFACE warpig_rtos)
