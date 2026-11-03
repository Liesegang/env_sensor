target_sources(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/sgp41.c
  ${CMAKE_CURRENT_SOURCE_DIR}/gas_indices.c
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/sensirion_gas_index_algorithm.c)
target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor)
