add_executable(keel_tests
    main.cpp
    error_test.cpp
    compose_test.cpp
)
target_link_libraries(keel_tests PRIVATE keel::keel)
if(MSVC)
    target_compile_options(keel_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(keel_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME keel_tests COMMAND keel_tests)

# Compile-fail tests: compile_fail/<name>.cpp must not compile, and the compiler output
# must match <pattern> (the test passes on the pattern alone). The targets are excluded
# from the normal build; each test builds its own.
function(keel_compile_fail name pattern)
    add_library(${name} OBJECT EXCLUDE_FROM_ALL compile_fail/${name}.cpp)
    target_link_libraries(${name} PRIVATE keel::keel)
    add_test(NAME compile_fail.${name}
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${name} --config $<CONFIG>)
    set_tests_properties(compile_fail.${name} PROPERTIES PASS_REGULAR_EXPRESSION "${pattern}")
endfunction()

keel_compile_fail(undeclared_dependency "requires T to be listed in this component")
keel_compile_fail(duplicate_dependency "a dependency is listed twice")
