# Pipes a scripted session into the shell and compares its output with the expected transcript.
execute_process(
  COMMAND ${SHELL}
  INPUT_FILE ${INPUT}
  OUTPUT_VARIABLE actual
  ERROR_VARIABLE errors
  RESULT_VARIABLE status
  TIMEOUT 30)

if(NOT status EQUAL 0)
  message(FATAL_ERROR "shell exited with ${status}\n${errors}")
endif()

file(READ ${EXPECTED} expected)
if(NOT actual STREQUAL expected)
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/shell_session.actual "${actual}")
  message(FATAL_ERROR "shell output differs from ${EXPECTED}\n"
    "actual output written to ${CMAKE_CURRENT_BINARY_DIR}/shell_session.actual\n${actual}")
endif()
