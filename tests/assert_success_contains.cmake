if(NOT DEFINED exe)
  message(FATAL_ERROR "exe is required")
endif()

if(NOT DEFINED config)
  message(FATAL_ERROR "config is required")
endif()

if(NOT DEFINED needles)
  message(FATAL_ERROR "needles is required")
endif()

execute_process(
  COMMAND "${exe}" --headless --list-models --config "${config}"
  RESULT_VARIABLE exit_code
  OUTPUT_VARIABLE stdout_text
  ERROR_VARIABLE stderr_text
)

if(NOT exit_code EQUAL 0)
  message(FATAL_ERROR
          "Expected success but command exited ${exit_code}\nstdout:\n${stdout_text}\nstderr:\n${stderr_text}")
endif()

foreach(needle IN LISTS needles)
  string(FIND "${stdout_text}" "${needle}" found_at)
  if(found_at EQUAL -1)
    message(FATAL_ERROR
            "Did not find expected text: ${needle}\nstdout:\n${stdout_text}\nstderr:\n${stderr_text}")
  endif()
endforeach()

if(DEFINED ordered_needles)
  set(previous_position -1)
  foreach(needle IN LISTS ordered_needles)
    string(FIND "${stdout_text}" "${needle}" found_at)
    if(found_at EQUAL -1)
      message(FATAL_ERROR
              "Did not find expected ordered text: ${needle}\nstdout:\n${stdout_text}\nstderr:\n${stderr_text}")
    endif()
    if(found_at LESS_EQUAL previous_position)
      message(FATAL_ERROR
              "Expected text after the preceding item: ${needle}\nstdout:\n${stdout_text}\nstderr:\n${stderr_text}")
    endif()
    set(previous_position ${found_at})
  endforeach()
endif()
