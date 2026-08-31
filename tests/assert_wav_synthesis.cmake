if(NOT DEFINED exe OR NOT DEFINED config OR NOT DEFINED out)
  message(FATAL_ERROR "Expected exe, config, and out variables")
endif()
if(NOT DEFINED runner AND NOT DEFINED model)
  message(FATAL_ERROR "Expected either runner or model to be defined")
endif()

if(EXISTS "${out}")
  file(REMOVE "${out}")
endif()

if(DEFINED model)
  set(selector_args --model "${model}")
else()
  set(selector_args --runner "${runner}")
endif()

if(stats)
  set(stats_args --stats)
endif()

if(NOT DEFINED text)
  set(text "Hello world")
endif()
if(NOT DEFINED expected_sample_frames)
  set(expected_sample_frames 4)
endif()
if(NOT DEFINED expected_size)
  # 44-byte canonical header + 8 payload bytes per stub runner chunk frame.
  math(EXPR expected_size "44 + (${expected_sample_frames} / 4) * 8")
endif()

execute_process(
  COMMAND "${exe}" --headless --config "${config}" ${selector_args} --synthesize "${text}" --out "${out}" ${stats_args}
  RESULT_VARIABLE exit_code
  OUTPUT_VARIABLE stdout_text
  ERROR_VARIABLE stderr_text
)

if(NOT exit_code EQUAL 0)
  message(FATAL_ERROR
          "Expected success but command exited ${exit_code}\nstdout:\n${stdout_text}\nstderr:\n${stderr_text}")
endif()

string(FIND "${stdout_text}" "Synthesized ${expected_sample_frames} sample frames" found_at)
if(found_at EQUAL -1)
  message(FATAL_ERROR "Did not find expected synthesis summary\nstdout:\n${stdout_text}")
endif()

if(stats)
  string(FIND "${stdout_text}" "Stats: peak RSS " stats_found_at)
  if(stats_found_at EQUAL -1)
    message(FATAL_ERROR "Did not find expected stats summary\nstdout:\n${stdout_text}")
  endif()
endif()

if(NOT EXISTS "${out}")
  message(FATAL_ERROR "Expected WAV output file to exist: ${out}")
endif()

file(SIZE "${out}" out_size)
if(NOT out_size EQUAL expected_size)
  message(FATAL_ERROR "Unexpected WAV file size ${out_size}, expected ${expected_size}")
endif()

file(READ "${out}" header_hex OFFSET 0 LIMIT 4 HEX)
if(NOT header_hex STREQUAL "52494646")  # "RIFF"
  message(FATAL_ERROR "WAV file does not start with a RIFF tag: ${header_hex}")
endif()

file(READ "${out}" wave_hex OFFSET 8 LIMIT 4 HEX)
if(NOT wave_hex STREQUAL "57415645")  # "WAVE"
  message(FATAL_ERROR "WAV file is missing the WAVE tag: ${wave_hex}")
endif()
