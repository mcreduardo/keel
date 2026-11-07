# Runs one example and checks its exit code and output (stdout and stderr together).
#
#   cmake -DEXE=<program> -DARGS=<arguments separated by |> -DEXIT=<expected exit code>
#         -DPATTERN=<regex the output must match> -P run_example.cmake
set(arguments "")
if(ARGS)
    string(REPLACE "|" ";" arguments "${ARGS}")
endif()
execute_process(
    COMMAND ${EXE} ${arguments}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
string(REPLACE "\r\n" "\n" output "${output}")  # Windows text-mode output

if(NOT result STREQUAL "${EXIT}")
    message(FATAL_ERROR "${EXE} ${arguments}: exit code ${result}, expected ${EXIT}:\n${output}")
endif()
if(NOT output MATCHES "${PATTERN}")
    message(FATAL_ERROR "${EXE} ${arguments}: output doesn't match '${PATTERN}':\n${output}")
endif()
