if(NOT DEFINED exe OR NOT DEFINED config OR NOT DEFINED needle)
  message(FATAL_ERROR "Expected exe, config, and needle variables")
endif()

execute_process(
  COMMAND "${exe}" --headless --config "${config}"
  RESULT_VARIABLE exit_code
  OUTPUT_VARIABLE stdout_text
  ERROR_VARIABLE stderr_text
)

set(combined_output "${stdout_text}${stderr_text}")

if(exit_code EQUAL 0)
  message(FATAL_ERROR "Expected a failing command but it exited successfully")
endif()

string(FIND "${combined_output}" "${needle}" needle_index)
if(needle_index EQUAL -1)
  message(FATAL_ERROR "Did not find expected text '${needle}' in output:\n${combined_output}")
endif()
