# Runs one compile-fail test: builds TARGET, which must fail, with output matching
# PATTERN. With COUNT_ERRORS (GCC/Clang output), it must also report exactly ERRORS
# errors: keel promises one error per mistake.
#
# Each test that composes something also asserts keel::isValid on the same list, before
# compose: false if the list itself is the mistake, true if the mistake is elsewhere (a
# component's body, a root call, run()). The guard's message is "isValid disagrees with
# compose", so the output must not mention isValid: neither that message nor (in the
# compiler's instantiation notes) an error that isValid caused itself.
#
#   cmake -DBUILD_DIR=... -DTARGET=... -DCONFIG=... -DPATTERN=... -DCOUNT_ERRORS=ON -DERRORS=1
#         -P compile_fail.cmake
set(config_args "")
if(CONFIG)
    set(config_args --config ${CONFIG})  # multi-config generators
endif()
execute_process(
    COMMAND ${CMAKE_COMMAND} --build ${BUILD_DIR} --target ${TARGET} ${config_args}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)

if(result EQUAL 0)
    message(FATAL_ERROR "${TARGET} compiled, but it must not")
endif()
if(NOT output MATCHES "${PATTERN}")
    message(FATAL_ERROR "${TARGET} failed without the expected diagnostic '${PATTERN}':\n${output}")
endif()
if(output MATCHES "isValid")
    message(FATAL_ERROR "${TARGET}: keel::isValid is part of the diagnostic:\n${output}")
endif()
if(COUNT_ERRORS)
    string(REGEX MATCHALL "error: " errors "${output}")
    list(LENGTH errors count)
    if(NOT count EQUAL ERRORS)
        message(FATAL_ERROR "${TARGET}: expected ${ERRORS} error(s), got ${count}:\n${output}")
    endif()
endif()
