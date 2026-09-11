add_executable(world_layers_test
  contract_test.cpp
  ../../source/world/world_layers.cpp
  ../../source/world/world_validation.cpp
  ../../source/world/world_document.cpp)
target_compile_features(world_layers_test PRIVATE cxx_std_20)
target_include_directories(world_layers_test PRIVATE ../../source)
target_link_libraries(world_layers_test PRIVATE nlohmann_json::nlohmann_json)
add_test(NAME world_layers_contract COMMAND world_layers_test
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures" "${CMAKE_CURRENT_BINARY_DIR}/scratch")
