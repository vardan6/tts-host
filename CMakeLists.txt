cmake_minimum_required(VERSION 3.28)

project(tts_host VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Without this, MSVC encodes \uXXXX universal-character-names in narrow
# string/char literals (e.g. the IPA phoneme constants in
# kokoro_phoneme_mapping.cpp) using the locale-dependent execution charset
# instead of UTF-8, silently corrupting them. Applied to every target,
# including FetchContent'd dependencies, since it's harmless for ASCII-only
# sources.
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

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

# ONNX Runtime ships prebuilt release archives with no top-level CMakeLists.txt
# (its lib/cmake/onnxruntime config package assumes an installed layout with
# lib64/ and include/onnxruntime/, which does not match the release archive's
# lib/ and include/ layout), so FetchContent_MakeAvailable's add_subdirectory
# step cannot be used. Populate the archive and define the IMPORTED target by
# hand instead.
set(ONNXRUNTIME_VERSION 1.29.0)

if(WIN32)
  FetchContent_Declare(
    onnxruntime
    URL https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-win-x64-${ONNXRUNTIME_VERSION}.zip
    URL_HASH SHA256=c9b4b7086b529ad814f428c1bad028e20a25d7dc0699836775faace4ab5b78b2
  )
else()
  FetchContent_Declare(
    onnxruntime
    URL https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-linux-x64-${ONNXRUNTIME_VERSION}.tgz
    URL_HASH SHA256=c3fddc4f139a045b0c4902c57410f0694f1c2fdf9b6939fbe38b1aeae7cd14ba
  )
endif()

FetchContent_Populate(onnxruntime)

add_library(onnxruntime::onnxruntime SHARED IMPORTED)
target_include_directories(
  onnxruntime::onnxruntime
  INTERFACE ${onnxruntime_SOURCE_DIR}/include
)

if(WIN32)
  set_target_properties(
    onnxruntime::onnxruntime PROPERTIES
    IMPORTED_LOCATION ${onnxruntime_SOURCE_DIR}/lib/onnxruntime.dll
    IMPORTED_IMPLIB ${onnxruntime_SOURCE_DIR}/lib/onnxruntime.lib
  )
  set(ONNXRUNTIME_RUNTIME_FILE ${onnxruntime_SOURCE_DIR}/lib/onnxruntime.dll)
else()
  set_target_properties(
    onnxruntime::onnxruntime PROPERTIES
    IMPORTED_LOCATION ${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.so.${ONNXRUNTIME_VERSION}
    IMPORTED_SONAME libonnxruntime.so.1
  )
  set(ONNXRUNTIME_RUNTIME_FILE ${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.so.${ONNXRUNTIME_VERSION})
endif()

# espeak-ng vendoring (Windows only, see docs/adr/0006-espeak-ng-vendoring-and-phoneme-mapping.md).
# The GitHub release publishes a single unversioned espeak-ng.msi asset (no
# Linux archive), extracted with 7-Zip because it reads the MSI's embedded
# CAB directly with no msiexec/Windows Installer dependency -- this works
# whether the configure step runs on Windows or WSL/Linux, unlike the
# onnxruntime archives above this only produces artifacts for Windows builds,
# so the whole block is gated on WIN32. Linux is out of scope (ADR 0006 point
# 2): it uses the system espeak-ng already required by scripts/setup-dev-env.sh.
set(ESPEAK_NG_RELEASE_TAG 1.52.0)

if(WIN32)
  find_program(SEVEN_ZIP_EXECUTABLE NAMES 7z 7z.exe)
  if(NOT SEVEN_ZIP_EXECUTABLE)
    message(FATAL_ERROR
      "7-Zip (7z) was not found on PATH. It is required to extract the "
      "vendored espeak-ng.msi (see docs/adr/0006-espeak-ng-vendoring-and-phoneme-mapping.md).")
  endif()

  FetchContent_Declare(
    espeak_ng_msi
    URL https://github.com/espeak-ng/espeak-ng/releases/download/${ESPEAK_NG_RELEASE_TAG}/espeak-ng.msi
    URL_HASH SHA256=7f673c709ea5dd579d3b5ebb98688cc575328a6ab7438d2bc405b88cedaeafb9
    DOWNLOAD_NO_EXTRACT TRUE
  )
  FetchContent_Populate(espeak_ng_msi)

  # The MSI extracts flat (443 files, no subdirectories): espeak_ng.exe,
  # libespeak_ng.dll, and per-language data files (phondata, phontab, en_dict,
  # ...) with no espeak-ng-data/ wrapper folder. Extract once into a build-tree
  # staging directory, then separate the two binaries from the data files so
  # the POST_BUILD copy below can synthesize the layout
  # default_espeak_ng_executable() expects (src/espeak_phonemizer.cpp):
  # espeak_ng.exe and libespeak_ng.dll next to the runner executable, and
  # every other file under an espeak-ng-data/ subdirectory beside them.
  set(ESPEAK_NG_EXTRACT_DIR ${CMAKE_BINARY_DIR}/_deps/espeak_ng-extracted)
  if(NOT EXISTS ${ESPEAK_NG_EXTRACT_DIR}/espeak_ng.exe)
    file(MAKE_DIRECTORY ${ESPEAK_NG_EXTRACT_DIR})
    execute_process(
      COMMAND ${SEVEN_ZIP_EXECUTABLE} x -y "${espeak_ng_msi_SOURCE_DIR}/espeak-ng.msi" -o${ESPEAK_NG_EXTRACT_DIR}
      RESULT_VARIABLE ESPEAK_NG_EXTRACT_RESULT
      OUTPUT_QUIET
    )
    if(NOT ESPEAK_NG_EXTRACT_RESULT EQUAL 0)
      message(FATAL_ERROR "7z failed to extract espeak-ng.msi (exit code ${ESPEAK_NG_EXTRACT_RESULT})")
    endif()
  endif()

  set(ESPEAK_NG_EXECUTABLE ${ESPEAK_NG_EXTRACT_DIR}/espeak_ng.exe)
  set(ESPEAK_NG_LIBRARY ${ESPEAK_NG_EXTRACT_DIR}/libespeak_ng.dll)

  # Stage every extracted file that is not one of the two binaries into an
  # espeak-ng-data/ directory, at configure time (FetchContent_Populate above
  # already ran, so the flat file list is enumerable now via file(GLOB)).
  set(ESPEAK_NG_DATA_STAGE_DIR ${CMAKE_BINARY_DIR}/_deps/espeak_ng-data-staged)
  if(NOT EXISTS ${ESPEAK_NG_DATA_STAGE_DIR})
    file(MAKE_DIRECTORY ${ESPEAK_NG_DATA_STAGE_DIR})
    file(GLOB ESPEAK_NG_ALL_FILES LIST_DIRECTORIES FALSE RELATIVE ${ESPEAK_NG_EXTRACT_DIR} ${ESPEAK_NG_EXTRACT_DIR}/*)
    foreach(ESPEAK_NG_FILE ${ESPEAK_NG_ALL_FILES})
      if(NOT ESPEAK_NG_FILE STREQUAL "espeak_ng.exe" AND NOT ESPEAK_NG_FILE STREQUAL "libespeak_ng.dll")
        file(COPY ${ESPEAK_NG_EXTRACT_DIR}/${ESPEAK_NG_FILE} DESTINATION ${ESPEAK_NG_DATA_STAGE_DIR})
      endif()
    endforeach()
  endif()
endif()

if(TARGET nlohmann_json_schema_validator::validator)
  set(JSON_SCHEMA_VALIDATOR_TARGET nlohmann_json_schema_validator::validator)
elseif(TARGET nlohmann_json_schema_validator)
  set(JSON_SCHEMA_VALIDATOR_TARGET nlohmann_json_schema_validator)
else()
  message(FATAL_ERROR "Could not find a JSON schema validator target")
endif()

add_executable(
  tts-host
  src/clipboard.cpp
  src/config_loader.cpp
  src/model_registry.cpp
  src/runner_protocol.cpp
  src/runner_launcher.cpp
  src/text_normalizer.cpp
  src/wav_writer.cpp
  src/main.cpp
)

add_executable(
  tts-host-stub-runner
  src/runner_audio_output.cpp
  src/runner_control_io.cpp
  src/runner_protocol.cpp
  src/stub_runner.cpp
  src/stub_runner_main.cpp
)

target_include_directories(tts-host-stub-runner PRIVATE include)
target_link_libraries(tts-host-stub-runner PRIVATE nlohmann_json::nlohmann_json)

# The real engine runner for the tts-host-<engine>-runner naming convention
# (src/main.cpp's runner_path_for_engine()). Speaks the same protocol as the
# stub runner but backed by an actual ONNX Runtime session, currently running
# the vendored placeholder identity model in place of real Kokoro-82M weights
# (not yet vendored, see roadmap.md).
add_executable(
  tts-host-kokoro-onnx-runner
  src/espeak_phonemizer.cpp
  src/kokoro_phoneme_mapping.cpp
  src/kokoro_runner.cpp
  src/kokoro_runner_main.cpp
  src/runner_audio_output.cpp
  src/runner_control_io.cpp
  src/runner_protocol.cpp
)

target_include_directories(tts-host-kokoro-onnx-runner PRIVATE include)
target_link_libraries(
  tts-host-kokoro-onnx-runner
  PRIVATE
    nlohmann_json::nlohmann_json
    onnxruntime::onnxruntime
)
if(NOT WIN32)
  set_target_properties(
    tts-host-kokoro-onnx-runner PROPERTIES
    BUILD_RPATH "$ORIGIN"
  )
endif()
add_custom_command(
  TARGET tts-host-kokoro-onnx-runner POST_BUILD
  COMMAND
    ${CMAKE_COMMAND} -E copy
    ${ONNXRUNTIME_RUNTIME_FILE}
    $<TARGET_FILE_DIR:tts-host-kokoro-onnx-runner>/$<IF:$<BOOL:${WIN32}>,onnxruntime.dll,libonnxruntime.so.1>
)

# Places the vendored espeak-ng binaries and data next to the runner
# executable, matching default_espeak_ng_executable()'s expected layout
# (src/espeak_phonemizer.cpp). Windows-only; see the FetchContent/7z block
# above and ADR 0006.
if(WIN32)
  add_custom_command(
    TARGET tts-host-kokoro-onnx-runner POST_BUILD
    COMMAND
      ${CMAKE_COMMAND} -E copy
      ${ESPEAK_NG_EXECUTABLE}
      ${ESPEAK_NG_LIBRARY}
      $<TARGET_FILE_DIR:tts-host-kokoro-onnx-runner>
    COMMAND
      ${CMAKE_COMMAND} -E copy_directory
      ${ESPEAK_NG_DATA_STAGE_DIR}
      $<TARGET_FILE_DIR:tts-host-kokoro-onnx-runner>/espeak-ng-data
  )
endif()

target_include_directories(tts-host PRIVATE include)

target_link_libraries(
  tts-host
  PRIVATE
    nlohmann_json::nlohmann_json
    ${JSON_SCHEMA_VALIDATOR_TARGET}
)
if(WIN32)
  target_link_libraries(tts-host PRIVATE user32)
endif()

if(MSVC)
  target_compile_options(tts-host PRIVATE /W4 /permissive-)
  target_compile_options(tts-host-stub-runner PRIVATE /W4 /permissive-)
  target_compile_options(tts-host-kokoro-onnx-runner PRIVATE /W4 /permissive-)
else()
  target_compile_options(tts-host PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tts-host-stub-runner PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tts-host-kokoro-onnx-runner PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Convenience target for the manual edit/build/run loop: `cmake --build
# build --config Debug --target list-models` rebuilds only what changed and
# runs the host against the registry fixture, without a full clean rebuild or
# the CTest suite (see compile-win.ps1 for the from-scratch verification run).
add_custom_target(
  list-models
  COMMAND $<TARGET_FILE:tts-host> --headless --list-models
          --config ${CMAKE_SOURCE_DIR}/tests/fixtures/registry/config.json
  DEPENDS tts-host
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  USES_TERMINAL
)

# Packaging: assembles the self-contained, zip distribution described in
# docs/design/architecture.md's "Distribution" section (host, its runner(s),
# schemas, an example config, and a portable.marker) and lets CPack zip it via
# the conventional `package` target. Reuses the exact runtime-file name/path
# the post-build copy commands above already produce next to each runner, so
# the installed layout matches what the host actually looks for at runtime.
# Currently mirrors today's flat "runner beside tts-host" convention
# (src/main.cpp's runner_path_for_engine()) rather than a per-engine
# subdirectory — revisit together if/when that convention changes.
install(TARGETS tts-host tts-host-kokoro-onnx-runner RUNTIME DESTINATION .)
install(
  FILES $<TARGET_FILE_DIR:tts-host-kokoro-onnx-runner>/$<IF:$<BOOL:${WIN32}>,onnxruntime.dll,libonnxruntime.so.1>
  DESTINATION .
)
if(WIN32)
  install(
    FILES ${ESPEAK_NG_EXECUTABLE} ${ESPEAK_NG_LIBRARY}
    DESTINATION .
  )
  install(DIRECTORY ${ESPEAK_NG_DATA_STAGE_DIR}/ DESTINATION espeak-ng-data)
endif()
install(DIRECTORY schemas DESTINATION .)
install(FILES config.example.json DESTINATION . RENAME config.json)
install(CODE "file(TOUCH \"\${CMAKE_INSTALL_PREFIX}/portable.marker\")")
if(EXISTS ${CMAKE_SOURCE_DIR}/models/kokoro-en-v1)
  install(DIRECTORY ${CMAKE_SOURCE_DIR}/models/kokoro-en-v1 DESTINATION models)
endif()

set(CPACK_PACKAGE_NAME "tts-host")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_FILE_NAME "tts-host-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/dist")
include(CPack)

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

  add_executable(
    tts-host-config-loader-tests
    src/config_loader.cpp
    tests/config_loader_tests.cpp
  )
  target_include_directories(tts-host-config-loader-tests PRIVATE include)
  target_link_libraries(
    tts-host-config-loader-tests
    PRIVATE
      nlohmann_json::nlohmann_json
      ${JSON_SCHEMA_VALIDATOR_TARGET}
  )
  if(MSVC)
    target_compile_options(tts-host-config-loader-tests PRIVATE /W4 /permissive-)
  else()
    target_compile_options(tts-host-config-loader-tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME tts_host_cli_text_sources COMMAND tts-host-config-loader-tests)

  add_executable(
    tts-host-espeak-phonemizer-tests
    src/espeak_phonemizer.cpp
    tests/espeak_phonemizer_tests.cpp
  )
  target_include_directories(tts-host-espeak-phonemizer-tests PRIVATE include)
  target_link_libraries(tts-host-espeak-phonemizer-tests PRIVATE nlohmann_json::nlohmann_json)
  if(MSVC)
    target_compile_options(tts-host-espeak-phonemizer-tests PRIVATE /W4 /permissive-)
  else()
    target_compile_options(tts-host-espeak-phonemizer-tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME tts_host_espeak_phonemizer COMMAND tts-host-espeak-phonemizer-tests)

  add_executable(
    tts-host-text-normalizer-tests
    src/text_normalizer.cpp
    tests/text_normalizer_tests.cpp
  )
  target_include_directories(tts-host-text-normalizer-tests PRIVATE include)
  if(MSVC)
    target_compile_options(tts-host-text-normalizer-tests PRIVATE /W4 /permissive-)
  else()
    target_compile_options(tts-host-text-normalizer-tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME tts_host_text_normalization COMMAND tts-host-text-normalizer-tests)

  add_executable(
    tts-host-kokoro-phoneme-mapping-tests
    src/kokoro_phoneme_mapping.cpp
    tests/kokoro_phoneme_mapping_tests.cpp
  )
  target_include_directories(tts-host-kokoro-phoneme-mapping-tests PRIVATE include)
  if(MSVC)
    target_compile_options(tts-host-kokoro-phoneme-mapping-tests PRIVATE /W4 /permissive-)
  else()
    target_compile_options(tts-host-kokoro-phoneme-mapping-tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME tts_host_kokoro_phoneme_mapping COMMAND tts-host-kokoro-phoneme-mapping-tests)

  # Proves the vendored ONNX Runtime toolchain (see the onnxruntime::onnxruntime
  # target above) with a standalone, non-protocol executable. Named distinctly
  # from tts-host-kokoro-onnx-runner (defined above), which speaks the runner
  # protocol.
  add_executable(tts-host-onnxruntime-smoketest tests/onnxruntime_smoketest_main.cpp)
  target_link_libraries(tts-host-onnxruntime-smoketest PRIVATE onnxruntime::onnxruntime)
  if(MSVC)
    target_compile_options(tts-host-onnxruntime-smoketest PRIVATE /W4 /permissive-)
  else()
    target_compile_options(tts-host-onnxruntime-smoketest PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  if(NOT WIN32)
    set_target_properties(
      tts-host-onnxruntime-smoketest PROPERTIES
      BUILD_RPATH "$ORIGIN"
    )
  endif()

  add_custom_command(
    TARGET tts-host-onnxruntime-smoketest POST_BUILD
    COMMAND
      ${CMAKE_COMMAND} -E copy
      ${ONNXRUNTIME_RUNTIME_FILE}
      $<TARGET_FILE_DIR:tts-host-onnxruntime-smoketest>/$<IF:$<BOOL:${WIN32}>,onnxruntime.dll,libonnxruntime.so.1>
  )

  add_test(
    NAME tts_host_onnxruntime_toolchain_smoketest
    COMMAND
      tts-host-onnxruntime-smoketest
      ${CMAKE_SOURCE_DIR}/tests/fixtures/kokoro_runner/placeholder.onnx
  )

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

  add_test(
    NAME tts_host_end_to_end_stub_synthesis
    COMMAND
      ${CMAKE_COMMAND}
      -Dexe=$<TARGET_FILE:tts-host>
      -Dconfig=${CMAKE_SOURCE_DIR}/config.example.json
      -Drunner=$<TARGET_FILE:tts-host-stub-runner>
      -Dout=${CMAKE_CURRENT_BINARY_DIR}/e2e-synthesis.wav
      -P ${CMAKE_SOURCE_DIR}/tests/assert_wav_synthesis.cmake
  )

  add_test(
    NAME tts_host_synthesis_selects_runner_by_engine
    COMMAND
      ${CMAKE_COMMAND}
      -Dexe=$<TARGET_FILE:tts-host>
      -Dconfig=${CMAKE_SOURCE_DIR}/tests/fixtures/registry/config.json
      -Dmodel=demo-package
      -Dout=${CMAKE_CURRENT_BINARY_DIR}/model-selected-synthesis.wav
      -P ${CMAKE_SOURCE_DIR}/tests/assert_wav_synthesis.cmake
  )

  add_test(
    NAME tts_host_synthesis_unknown_model
    COMMAND
      ${CMAKE_COMMAND}
      -Dexe=$<TARGET_FILE:tts-host>
      -Dconfig=${CMAKE_SOURCE_DIR}/tests/fixtures/registry/config.json
      "-Dargs=--model;does-not-exist;--synthesize;Hello world;--out;${CMAKE_CURRENT_BINARY_DIR}/unknown-model.wav"
      -Dneedle=Unknown model id
      -P ${CMAKE_SOURCE_DIR}/tests/assert_failure_contains.cmake
  )

  add_test(
    NAME tts_host_model_and_runner_mutually_exclusive
    COMMAND
      ${CMAKE_COMMAND}
      -Dexe=$<TARGET_FILE:tts-host>
      -Dconfig=${CMAKE_SOURCE_DIR}/tests/fixtures/registry/config.json
      "-Dargs=--model;demo-package;--runner;$<TARGET_FILE:tts-host-stub-runner>;--synthesize;Hello world;--out;${CMAKE_CURRENT_BINARY_DIR}/conflicting-selectors.wav"
      -Dneedle=mutually exclusive
      -P ${CMAKE_SOURCE_DIR}/tests/assert_failure_contains.cmake
  )
endif()
