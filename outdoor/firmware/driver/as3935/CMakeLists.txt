target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/as3935.c
                           ${CMAKE_CURRENT_SOURCE_DIR}/as3935_decode.c)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
