target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/bme690.c
                           ${CMAKE_CURRENT_SOURCE_DIR}/vendor/bme69x.c)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
                                      ${CMAKE_CURRENT_SOURCE_DIR}/vendor)
target_compile_definitions(app PRIVATE BME69X_DO_NOT_USE_FPU)
