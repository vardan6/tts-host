# Runs tts-host without --headless on a platform without a tray icon and
# expects the clear "not implemented" error. The host binds its API before
# starting the tray, so the config is a copy of the shipped example on a port
# of its own: the example's 7861 collides with a running Host.

if(NOT DEFINED exe OR NOT DEFINED example OR NOT DEFINED schemas OR NOT DEFINED work)
  message(FATAL_ERROR "Expected exe, example, schemas, and work variables")
endif()

file(REMOVE_RECURSE "${work}")
file(READ "${example}" config_text)
string(JSON config_text SET "${config_text}" "$schema" "\"${schemas}/config.schema.json\"")
string(JSON config_text SET "${config_text}" server port 47861)
set(config "${work}/config.json")
file(WRITE "${config}" "${config_text}")

execute_process(
  COMMAND "${exe}" --config "${config}"
  RESULT_VARIABLE exit_code
  OUTPUT_VARIABLE stdout_text
  ERROR_VARIABLE stderr_text
)
set(combined_output "${stdout_text}${stderr_text}")
string(FIND "${combined_output}" "tray icon is not implemented on this platform yet" found_at)
if(found_at EQUAL -1)
  message(FATAL_ERROR "Expected the tray-unavailable error, got:\n${combined_output}")
endif()
