add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/firmware/telemetry
                 ${CMAKE_CURRENT_BINARY_DIR}/shared_telemetry)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/telemetry_encode.c)
if(CONFIG_APP_BLE)
  target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/telemetry_ble.c)
endif()
