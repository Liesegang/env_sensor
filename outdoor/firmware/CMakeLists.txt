cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
# NCS 3.4.1 caches these generated names without Kconfig string quotes.
# Clear its internal cache entries before Zephyr interprets them as CLI options.
foreach(crypto_header MBEDTLS_CONFIG_FILE TF_PSA_CRYPTO_CONFIG_FILE TF_PSA_CRYPTO_USER_CONFIG_FILE)
  unset(CONFIG_${crypto_header} CACHE)
endforeach()
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(env_sensor_outdoor)

target_sources(app PRIVATE src/main.c)
add_subdirectory(driver)
add_subdirectory(telemetry)

add_subdirectory(logging)
