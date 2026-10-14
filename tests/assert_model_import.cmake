# Proves --import-model end to end through the real CLI: a package outside any
# model directory is imported into the configured one and then listed by
# --list-models, and a second import of the same package is refused with an
# actionable reason. Everything it needs is generated under ${work}, so the
# test never writes into the source tree.

if(NOT DEFINED exe OR NOT DEFINED schemas OR NOT DEFINED work)
  message(FATAL_ERROR "Expected exe, schemas, and work variables")
endif()

file(REMOVE_RECURSE "${work}")

set(source_package "${work}/source/cli-imported-package")
file(WRITE "${source_package}/model.json"
"{
  \"$schema\": \"${schemas}/model.schema.json\",
  \"schemaVersion\": 1,
  \"id\": \"cli-imported-package\",
  \"displayName\": \"CLI Imported Package\",
  \"engine\": \"stub\",
  \"languages\": [\"en\"],
  \"files\": { \"model\": \"model.bin\" },
  \"license\": { \"name\": \"Apache-2.0\", \"url\": \"https://example.com/cli-imported-package\" }
}
")
file(WRITE "${source_package}/model.bin" "stub weights")

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
    \"fast\": { \"model\": \"cli-imported-package\", \"device\": \"cpu\", \"loadPolicy\": \"eager\" }
  },
  \"languageDefaults\": { \"en\": \"fast\" }
}
")

execute_process(
  COMMAND "${exe}" --headless --import-model "${source_package}" --list-models --config "${config}"
  RESULT_VARIABLE import_exit_code
  OUTPUT_VARIABLE import_stdout
  ERROR_VARIABLE import_stderr
)

if(NOT import_exit_code EQUAL 0)
  message(FATAL_ERROR
          "Expected the import to succeed but it exited ${import_exit_code}\nstdout:\n${import_stdout}\nstderr:\n${import_stderr}")
endif()

foreach(needle
        "Imported CLI Imported Package (cli-imported-package)"
        "Discovered model packages: 1"
        "OK  cli-imported-package (stub, en)")
  string(FIND "${import_stdout}" "${needle}" found_at)
  if(found_at EQUAL -1)
    message(FATAL_ERROR "Did not find expected text: ${needle}\nstdout:\n${import_stdout}")
  endif()
endforeach()

if(NOT EXISTS "${work}/registry/cli-imported-package/model.bin")
  message(FATAL_ERROR "The imported package's weights were not copied into the model directory")
endif()

execute_process(
  COMMAND "${exe}" --headless --import-model "${source_package}" --config "${config}"
  RESULT_VARIABLE duplicate_exit_code
  OUTPUT_VARIABLE duplicate_stdout
  ERROR_VARIABLE duplicate_stderr
)

if(duplicate_exit_code EQUAL 0)
  message(FATAL_ERROR "Expected the duplicate import to fail but it succeeded")
endif()

string(FIND "${duplicate_stdout}${duplicate_stderr}" "already installed" duplicate_needle_index)
if(duplicate_needle_index EQUAL -1)
  message(FATAL_ERROR
          "Duplicate import did not report an actionable reason:\n${duplicate_stdout}${duplicate_stderr}")
endif()
