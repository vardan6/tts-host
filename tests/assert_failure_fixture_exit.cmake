# Exercise the fixture's exception boundary without depending on a cancellation race.
set(input "${scratch}/failure-fixture-invalid-control.txt")
# No newline translation on Windows: an oversized header is invalid on every platform.
string(REPEAT "X" 65536 invalid_header)
file(WRITE "${input}" "${invalid_header}")
execute_process(
  COMMAND "${runner}"
  INPUT_FILE "${input}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
  RESULT_VARIABLE result
  TIMEOUT 5)
file(REMOVE "${input}")
if(NOT "${result}" STREQUAL "1")
  message(FATAL_ERROR "Fixture must exit with failure, without aborting: ${result}; ${error}")
endif()
if("${error}" STREQUAL "")
  message(FATAL_ERROR "Fixture did not report the exception on stderr")
endif()
