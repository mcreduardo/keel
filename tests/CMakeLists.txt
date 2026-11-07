add_executable(keel_tests
    main.cpp
    error_test.cpp
    compose_test.cpp
    lifecycle_test.cpp
    optional_test.cpp
)
target_link_libraries(keel_tests PRIVATE keel::keel)
if(MSVC)
    target_compile_options(keel_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(keel_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME keel_tests COMMAND keel_tests)

# Compile-fail tests: compile_fail/<name>.cpp must not compile, the output must match
# <pattern>, and (GCC/Clang) there must be exactly one error, or [errors] if given. The
# targets are excluded from the normal build; each test builds its own. They share the
# build tree, so they don't run in parallel.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set(KEEL_COUNT_ERRORS ON)
else()
    set(KEEL_COUNT_ERRORS OFF)
endif()

function(keel_compile_fail name pattern)
    set(errors 1)
    if(ARGC GREATER 2)
        set(errors ${ARGV2})
    endif()
    add_library(${name} OBJECT EXCLUDE_FROM_ALL compile_fail/${name}.cpp)
    target_link_libraries(${name} PRIVATE keel::keel)
    add_test(NAME compile_fail.${name}
             COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -DTARGET=${name} -DCONFIG=$<CONFIG>
                     "-DPATTERN=${pattern}" -DCOUNT_ERRORS=${KEEL_COUNT_ERRORS} -DERRORS=${errors}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail.cmake)
    set_tests_properties(compile_fail.${name} PROPERTIES RESOURCE_LOCK keel_build_tree)
endfunction()

# MSVC prints template arguments without a space after the comma: ", ?" matches both.
keel_compile_fail(undeclared_dependency "requires T to be listed in this component")
keel_compile_fail(duplicate_dependency "a dependency is listed twice")
keel_compile_fail(missing_dependency "MissingDependency<Server, ?Logger>")
keel_compile_fail(missing_dependencies "MissingDependency<Server, ?Logger>.*MissingDependency<Exporter, ?Config>" 2)
keel_compile_fail(dependency_cycle "DependencyCycle<keel::detail::TypeList<P, ?Q, ?R> ?>")
keel_compile_fail(two_cycles "DependencyCycle<keel::detail::TypeList<A, ?B, ?C, ?D> ?>")
keel_compile_fail(duplicate_component "DuplicateComponent<Logger>")
keel_compile_fail(not_a_component "NotAComponent<Settings>")
keel_compile_fail(private_component "NotAComponent<Hidden>")
keel_compile_fail(abstract_component "AbstractComponent<Sink>")
keel_compile_fail(throwing_deinit "exception specification")
keel_compile_fail(get_component_on_optional "T is declared keel::Optional<T>; use findComponent<T>")
keel_compile_fail(undeclared_optional "findComponent<T>\\(\\) requires keel::Optional<T>")
keel_compile_fail(required_and_optional "a dependency is listed twice")
keel_compile_fail(find_component_on_required "T is a required dependency; use getComponent<T>")
keel_compile_fail(nested_optional "InvalidOptional<Server, ?keel::Optional<Metrics> ?>")
keel_compile_fail(const_optional "InvalidOptional<Server, ?const Metrics>")
keel_compile_fail(optional_cycle "DependencyCycle<keel::detail::TypeList<Store, ?Cache> ?>")
