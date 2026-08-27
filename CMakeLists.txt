cmake_minimum_required(VERSION 3.28)

project(tts_host VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.tar.gz
)

FetchContent_Declare(
  json_schema_validator
  URL https://github.com/pboettch/json-schema-validator/archive/refs/tags/2.4.0.tar.gz
)

FetchContent_MakeAvailable(nlohmann_json json_schema_validator)

if(TARGET nlohmann_json_schema_validator::validator)
  set(JSON_SCHEMA_VALIDATOR_TARGET nlohmann_json_schema_validator::validator)
elseif(TARGET nlohmann_json_schema_validator)
  set(JSON_SCHEMA_VALIDATOR_TARGET nlohmann_json_schema_validator)
else()
  message(FATAL_ERROR "Could not find a JSON schema validator target")
endif()

add_executable(
  tts-host
  src/config_loader.cpp
  src/model_registry.cpp
  src/runner_protocol.cpp
  src/main.cpp
)

add_executable(
  tts-host-stub-runner
  src/runner_audio_output.cpp
  src/runner_protocol.cpp
  src/stub_runner.cpp
  src/stub_runner_main.cpp
)

target_include_directories(tts-host-stub-runner PRIVATE include)
target_link_libraries(tts-host-stub-runner PRIVATE nlohmann_json::nlohmann_json)

target_include_directories(tts-host PRIVATE include)

target_link_libraries(
  tts-host
  PRIVATE
    nlohmann_json::nlohmann_json
    ${JSON_SCHEMA_VALIDATOR_TARGET}
)

if(MSVC)
  target_compile_options(tts-host PRIVATE /W4 /permissive-)
  target_compile_options(tts-host-stub-runner PRIVATE /W4 /permissive-)
else()
  target_compile_options(tts-host PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tts-host-stub-runner PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  add_executable(
    tts-host-runner-protocol-tests
    src/runner_audio_output.cpp
    src/runner_protocol.cpp
    src/stub_runner.cpp
    tests/runner_protocol_tests.cpp
  )
  target_include_directories(tts-host-runner-protocol-tests PRIVATE include)
  target_link_libraries(tts-host-runner-protocol-tests PRIVATE nlohmann_json::nlohmann_json)
  if(MSVC)
    target_compile_options(tts-host-runner-protocol-tests PRIVATE /W4 /permissive-)
  else()
    target_compile_options(tts-host-runner-protocol-tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()

  add_test(NAME tts_host_runner_control_framing COMMAND tts-host-runner-protocol-tests)

  add_test(
    NAME tts_host_valid_config
    COMMAND tts-host --headless --config ${CMAKE_SOURCE_DIR}/config.example.json
  )

  add_test(
    NAME tts_host_valid_example_model_manifest
    COMMAND
      ${CMAKE_COMMAND}
      -Dexe=$<TARGET_FILE:tts-host>
      -Dconfig=${CMAKE_SOURCE_DIR}/tests/fixtures/registry/config.json
      "-Dneedles=OK  alternate-package (kokoro-onnx, en);OK  demo-package (kokoro-onnx, en)"
      "-Dordered_needles=OK  alternate-package (kokoro-onnx, en);OK  demo-package (kokoro-onnx, en)"
      -P ${CMAKE_SOURCE_DIR}/tests/assert_success_contains.cmake
  )

  add_test(
    NAME tts_host_invalid_port
    COMMAND
      ${CMAKE_COMMAND}
      -Dexe=$<TARGET_FILE:tts-host>
      -Dconfig=${CMAKE_SOURCE_DIR}/tests/invalid-port-config.json
      -Dneedle=/server/port
      -P ${CMAKE_SOURCE_DIR}/tests/assert_failure_contains.cmake
  )

  add_test(
    NAME tts_host_unsupported_schema_version
    COMMAND
      ${CMAKE_COMMAND}
      -Dexe=$<TARGET_FILE:tts-host>
      -Dconfig=${CMAKE_SOURCE_DIR}/tests/unsupported-schema-version.json
      -Dneedle=/schemaVersion
      -P ${CMAKE_SOURCE_DIR}/tests/assert_failure_contains.cmake
  )

  add_test(
    NAME tts_host_list_models
    COMMAND
      ${CMAKE_COMMAND}
      -Dexe=$<TARGET_FILE:tts-host>
      -Dconfig=${CMAKE_SOURCE_DIR}/tests/fixtures/registry/config.json
      "-Dneedles=Discovered model packages: 2;OK  alternate-package (kokoro-onnx, en);OK  demo-package (kokoro-onnx, en);Unsupported registry entries: 5;missing model.json;registry entry is not a directory;registry directory does not exist;schema validation failed at /: required property 'displayName' not found in object;manifest file path /files/model escapes the package root"
      "-Dordered_needles=OK  alternate-package (kokoro-onnx, en);OK  demo-package (kokoro-onnx, en);escaping-file-path;invalid-manifest"
      -P ${CMAKE_SOURCE_DIR}/tests/assert_success_contains.cmake
  )
endif()
