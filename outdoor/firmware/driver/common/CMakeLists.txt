target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sensor_codec.c
                           ${CMAKE_CURRENT_SOURCE_DIR}/sensor_bus.c)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
