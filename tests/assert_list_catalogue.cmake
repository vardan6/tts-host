# Proves --list-catalogue end to end through the real CLI: the compiled-in
# catalogue is listed with its licence and download size, an entry the registry
# does not have is offered, and the same entry is reported as already installed
# once a package carrying its id is discovered. Everything it needs is generated
# under ${work}, so the test never writes into the source tree.

if(NOT DEFINED exe OR NOT DEFINED schemas OR NOT DEFINED work)
  message(FATAL_ERROR "Expected exe, schemas, and work variables")
endif()

file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}/registry")

set(config "${work}/config.json")
file(WRITE "${config}"
"{
  \"$schema\": \"${schemas}/config.schema.json\",
  \"schemaVersion\": 1,
  \"server\": {
    \"host\": \"127.0.0.1\",
    \"port\": 7862,
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
    \"fast\": { \"model\": \"kokoro-en-v1\", \"device\": \"cpu\", \"loadPolicy\": \"eager\" }
  },
  \"languageDefaults\": { \"en\": \"fast\" }
}
")

execute_process(
  COMMAND "${exe}" --headless --list-catalogue --config "${config}"
  RESULT_VARIABLE offered_exit_code
  OUTPUT_VARIABLE offered_stdout
  ERROR_VARIABLE offered_stderr
)

if(NOT offered_exit_code EQUAL 0)
  message(FATAL_ERROR
          "Expected --list-catalogue to succeed but it exited ${offered_exit_code}\nstdout:\n${offered_stdout}\nstderr:\n${offered_stderr}")
endif()

foreach(needle
        "Downloadable model packages: 1"
        "GET  kokoro-en-v1 (kokoro-onnx, en, "
        " MB)"
        "Kokoro-82M (English) :: Apache-2.0 (https://huggingface.co/")
  string(FIND "${offered_stdout}" "${needle}" found_at)
  if(found_at EQUAL -1)
    message(FATAL_ERROR "Did not find expected text: ${needle}\nstdout:\n${offered_stdout}")
  endif()
endforeach()

# The same id installed in the registry: the entry is still listed -- with its
# licence -- but reported as present rather than offered for download.
set(installed_package "${work}/registry/kokoro-en-v1")
file(WRITE "${installed_package}/model.json"
"{
  \"$schema\": \"${schemas}/model.schema.json\",
  \"schemaVersion\": 1,
  \"id\": \"kokoro-en-v1\",
  \"displayName\": \"Kokoro-82M (English)\",
  \"engine\": \"kokoro-onnx\",
  \"languages\": [\"en\"],
  \"files\": { \"model\": \"model.onnx\" },
  \"license\": { \"name\": \"Apache-2.0\", \"url\": \"https://example.com/kokoro\" }
}
")
file(WRITE "${installed_package}/model.onnx" "stub weights")

execute_process(
  COMMAND "${exe}" --headless --list-catalogue --config "${config}"
  RESULT_VARIABLE installed_exit_code
  OUTPUT_VARIABLE installed_stdout
  ERROR_VARIABLE installed_stderr
)

if(NOT installed_exit_code EQUAL 0)
  message(FATAL_ERROR
          "Expected --list-catalogue to succeed but it exited ${installed_exit_code}\nstdout:\n${installed_stdout}\nstderr:\n${installed_stderr}")
endif()

string(FIND "${installed_stdout}" "HAVE kokoro-en-v1" installed_needle_index)
if(installed_needle_index EQUAL -1)
  message(FATAL_ERROR
          "An installed package was still offered for download:\n${installed_stdout}")
endif()
