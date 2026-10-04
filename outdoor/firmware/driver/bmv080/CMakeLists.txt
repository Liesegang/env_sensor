target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/bmv080_driver.c)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(CONFIG_APP_BMV080)
  foreach(sdk_file
      "${CONFIG_BMV080_SDK_INCLUDE_DIR}/bmv080.h"
      "${CONFIG_BMV080_SDK_INCLUDE_DIR}/bmv080_defs.h"
      "${CONFIG_BMV080_SDK_DRIVER_LIBRARY}"
      "${CONFIG_BMV080_SDK_POSTPROCESSOR_LIBRARY}")
    if(NOT EXISTS "${sdk_file}")
      message(FATAL_ERROR "BMV080 SDK file is missing: ${sdk_file}")
    endif()
  endforeach()
  if(CONFIG_MAIN_STACK_SIZE LESS 12288)
    message(FATAL_ERROR "The BMV080 SDK needs CONFIG_MAIN_STACK_SIZE >= 12288")
  endif()
  target_include_directories(app PRIVATE ${CONFIG_BMV080_SDK_INCLUDE_DIR})
  zephyr_link_libraries(
    ${CONFIG_BMV080_SDK_DRIVER_LIBRARY}
    ${CONFIG_BMV080_SDK_POSTPROCESSOR_LIBRARY})
endif()
