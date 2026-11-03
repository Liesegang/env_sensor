cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
# NCS 3.4.1 may cache these generated header names without Kconfig quotes.
foreach(crypto_header MBEDTLS_CONFIG_FILE TF_PSA_CRYPTO_CONFIG_FILE TF_PSA_CRYPTO_USER_CONFIG_FILE)
  unset(CONFIG_${crypto_header} CACHE)
endforeach()
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(env_sensor_card)

target_sources(app PRIVATE src/main.c src/sd_logger.c src/switch_test.c src/pmic.c
                           src/card_display.c src/card_ble.c src/co2_alarm.c src/card_image_transfer.c
                           src/card_history.c)

set(SHARED_SENSOR_DRIVER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/firmware/driver")
foreach(driver common opt4001 bme690 sgp41 stcc4 sht45 sfa40 epd3in6e epd_dashboard)
  add_subdirectory("${SHARED_SENSOR_DRIVER_DIR}/${driver}"
                   "${CMAKE_CURRENT_BINARY_DIR}/shared_${driver}")
endforeach()

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/firmware/telemetry"
                 "${CMAKE_CURRENT_BINARY_DIR}/shared_telemetry")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/firmware/history"
                 "${CMAKE_CURRENT_BINARY_DIR}/shared_history")
