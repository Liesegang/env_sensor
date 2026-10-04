target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sfa40.c
                           ${CMAKE_CURRENT_SOURCE_DIR}/sfa40_decode.c)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
