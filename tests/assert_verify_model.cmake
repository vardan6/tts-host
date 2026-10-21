# Proves --verify-model through the real CLI rejects what a stale release stage
# would hold (docs/design/architecture.md#distribution): a catalogue install at
# another revision, and one at the catalogue revision whose files differ. The
# real weights are too large for a test, so no package here passes. Everything
# is generated under ${work}.

if(NOT DEFINED exe OR NOT DEFINED schemas OR NOT DEFINED work)
  message(FATAL_ERROR "Expected exe, schemas, and work variables")
endif()

file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}/registry" "${work}/data")

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

function(write_package revision)
  file(WRITE "${work}/registry/kokoro-en-v1/model.json"
"{
  \"$schema\": \"${schemas}/model.schema.json\",
  \"schemaVersion\": 1,
  \"id\": \"kokoro-en-v1\",
  \"displayName\": \"Kokoro-82M (English)\",
  \"engine\": \"kokoro-onnx\",
  \"languages\": [\"en\"],
  \"revision\": \"${revision}\",
  \"files\": { \"model\": \"model.onnx\" },
  \"license\": { \"name\": \"Apache-2.0\", \"url\": \"https://example.com/kokoro\" }
}
")
  file(WRITE "${work}/registry/kokoro-en-v1/model.onnx" "stub weights")
endfunction()

function(verify_expect_failure needle out_stderr)
  execute_process(
    COMMAND "${exe}" --headless --verify-model kokoro-en-v1 --config "${config}" --data-dir "${work}/data"
    RESULT_VARIABLE exit_code
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
  )
  if(exit_code EQUAL 0)
    message(FATAL_ERROR "Expected --verify-model to fail\nstdout:\n${stdout}\nstderr:\n${stderr}")
  endif()
  string(FIND "${stderr}" "${needle}" found_at)
  if(found_at EQUAL -1)
    message(FATAL_ERROR "Did not find expected text: ${needle}\nstdout:\n${stdout}\nstderr:\n${stderr}")
  endif()
  set(${out_stderr} "${stderr}" PARENT_SCOPE)
endfunction()

# Another revision: the package a previous release staged.
write_package("0000000000000000000000000000000000000000000000000000000000000000")
verify_expect_failure("is at revision 0000000000000000000000000000000000000000000000000000000000000000; the catalogue has " stale)
string(REGEX MATCH "the catalogue has ([0-9a-f]+)" matched "${stale}")
if(NOT CMAKE_MATCH_1)
  message(FATAL_ERROR "No catalogue revision in:\n${stale}")
endif()

# The catalogue revision, but stub content: every catalogue file is reported.
write_package("${CMAKE_MATCH_1}")
verify_expect_failure("differs from the catalogue:" damaged)
foreach(needle "model.onnx (differs)" " (missing)")
  string(FIND "${damaged}" "${needle}" found_at)
  if(found_at EQUAL -1)
    message(FATAL_ERROR "Did not find expected text: ${needle}\nstderr:\n${damaged}")
  endif()
endforeach()
