# Proves the request-language rule end to end through the real CLI
# (docs/design/architecture.md#speech-pipeline): explicit --language, else the
# text's script, else the configured default, each naming a languageDefaults
# profile and so a model. Everything it needs is generated under ${work}, so
# the test never writes into the source tree.
#
# Text is fed through --stdin rather than --synthesize on purpose: stdin is
# read as UTF-8 bytes, while a non-ASCII command-line argument would depend on
# the process code page on Windows.

if(NOT DEFINED exe OR NOT DEFINED schemas OR NOT DEFINED work)
  message(FATAL_ERROR "Expected exe, schemas, and work variables")
endif()

file(REMOVE_RECURSE "${work}")

function(write_stub_package id language)
  file(WRITE "${work}/registry/${id}/model.json"
"{
  \"$schema\": \"${schemas}/model.schema.json\",
  \"schemaVersion\": 1,
  \"id\": \"${id}\",
  \"displayName\": \"${id}\",
  \"engine\": \"stub\",
  \"languages\": [\"${language}\"],
  \"files\": { \"model\": \"model.bin\" },
  \"license\": { \"name\": \"Apache-2.0\", \"url\": \"https://example.com/${id}\" }
}
")
  file(WRITE "${work}/registry/${id}/model.bin" "stub weights")
endfunction()

write_stub_package(en-package en)
write_stub_package(ru-package ru)

set(config "${work}/config.json")
file(WRITE "${config}"
"{
  \"$schema\": \"${schemas}/config.schema.json\",
  \"schemaVersion\": 1,
  \"server\": {
    \"host\": \"127.0.0.1\",
    \"port\": 7861,
    \"authentication\": \"none\",
    \"allowedOrigins\": []
  },
  \"audio\": { \"outputDevice\": \"system-default\" },
  \"modelRegistry\": {
    \"directories\": [\"${work}/registry\"],
    \"watchForChanges\": false,
    \"idleUnloadSeconds\": 600,
    \"maximumLoadedGpuModels\": 1
  },
  \"profiles\": {
    \"fast\": { \"model\": \"en-package\", \"device\": \"cpu\", \"loadPolicy\": \"eager\" },
    \"quality\": { \"model\": \"missing-gpu-package\", \"device\": \"auto\", \"loadPolicy\": \"onDemand\", \"fallbackProfile\": \"fast\" },
    \"russian\": { \"model\": \"ru-package\", \"device\": \"cpu\", \"loadPolicy\": \"onDemand\" }
  },
  \"languageDefaults\": { \"en\": \"quality\", \"ru\": \"russian\" }
}
")

file(WRITE "${work}/latin.txt" "Hello world, this is a test.")
file(WRITE "${work}/cyrillic.txt" "Привет, мир, это проверка.")

# Runs one synthesis and returns its combined output, requiring the exit code
# the caller expects.
function(synthesize input_file expect_success output_variable)
  execute_process(
    COMMAND "${exe}" --headless --stdin --out "${work}/out.wav" --config "${config}" ${ARGN}
    INPUT_FILE "${input_file}"
    RESULT_VARIABLE exit_code
    OUTPUT_VARIABLE stdout_text
    ERROR_VARIABLE stderr_text
  )
  if(expect_success AND NOT exit_code EQUAL 0)
    message(FATAL_ERROR
            "Expected synthesis to succeed but it exited ${exit_code}\nstdout:\n${stdout_text}\nstderr:\n${stderr_text}")
  endif()
  if(NOT expect_success AND exit_code EQUAL 0)
    message(FATAL_ERROR "Expected synthesis to fail but it succeeded\nstdout:\n${stdout_text}")
  endif()
  set(${output_variable} "${stdout_text}${stderr_text}" PARENT_SCOPE)
endfunction()

function(expect_output text needle)
  string(FIND "${text}" "${needle}" found_at)
  if(found_at EQUAL -1)
    message(FATAL_ERROR "Did not find expected text: ${needle}\noutput:\n${text}")
  endif()
endfunction()

synthesize("${work}/cyrillic.txt" TRUE cyrillic_output)
expect_output("${cyrillic_output}"
              "Language: ru (detected from text script), profile russian, model ru-package")

synthesize("${work}/latin.txt" TRUE latin_output)
expect_output("${latin_output}"
              "Language: en (detected from text script), profile quality, model missing-gpu-package")
expect_output("${latin_output}"
              "Profile quality is unavailable (Unknown model id: missing-gpu-package); using fallback profile fast, model en-package")

synthesize("${work}/latin.txt" TRUE explicit_output --language ru)
expect_output("${explicit_output}" "Language: ru (explicit), profile russian, model ru-package")

synthesize("${work}/latin.txt" FALSE unconfigured_output --language hy)
expect_output("${unconfigured_output}" "languageDefaults has no entry for language: hy")
