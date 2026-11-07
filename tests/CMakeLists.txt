add_executable(keel_tests
    main.cpp
    error_test.cpp
    compose_test.cpp
    lifecycle_test.cpp
    optional_test.cpp
    init_after_test.cpp
    run_test.cpp
    interfaces_test.cpp
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

# MSVC prints template arguments without a space after the comma: ", ?" matches both. It
# also adds a calling convention to member function pointers: "int (__cdecl Cli::*)()".
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
keel_compile_fail(nested_optional "InvalidDependency<Server, ?keel::Optional<keel::Optional<Metrics> ?> ?>")
keel_compile_fail(const_optional "InvalidDependency<Server, ?keel::Optional<const Metrics> ?>")
keel_compile_fail(optional_cycle "DependencyCycle<keel::detail::TypeList<Store, ?Cache> ?>")
keel_compile_fail(get_component_on_init_after "T is declared keel::InitAfter<T>, which is ordering only; replace it with T to use T")
keel_compile_fail(find_component_on_init_after "T is declared keel::InitAfter<T>, which is ordering only; replace it with keel::Optional<T>")
keel_compile_fail(required_and_init_after "a dependency is listed twice")
keel_compile_fail(init_after_cycle "DependencyCycle<keel::detail::TypeList<Pool, ?Worker> ?>")
keel_compile_fail(invalid_init_after "InvalidDependency<Server, ?keel::InitAfter<keel::Optional<Metrics> ?> ?>")
keel_compile_fail(throwing_stop "exception specification")
keel_compile_fail(run_member_of_non_component "RunTargetNotComposed<Cli, ?int")
keel_compile_fail(run_member_not_callable "RunTargetNotCallable<int ?\\(.*Cli::\\*\\)\\(int\\)")
keel_compile_fail(run_rvalue_member "RunTargetNotCallable<int ?\\(.*Cli::\\*\\)\\(\\) ?&&")
keel_compile_fail(run_not_callable "RunNotCallable<")
keel_compile_fail(run_not_an_app "NotAnApp<Cli>")
keel_compile_fail(run_generic_lambda_error "nope")
keel_compile_fail(ambiguous_dependency "AmbiguousDependency<Service, ?Storage, ?keel::detail::TypeList<DiskStorage, ?MemoryStorage ?> ?>")
keel_compile_fail(ambiguous_optional "AmbiguousDependency<Service, ?Storage, ?keel::detail::TypeList<DiskStorage, ?MemoryStorage ?> ?>")
keel_compile_fail(provides_without_deriving "NotDerivedFromInterface<DiskStorage, ?Storage>")
keel_compile_fail(provides_private_base "NotDerivedFromInterface<DiskStorage, ?Storage>")
keel_compile_fail(provides_ambiguous_base "NotDerivedFromInterface<DiskStorage, ?Storage>")
keel_compile_fail(duplicate_provides "keel::Provides<I> is listed twice")
keel_compile_fail(invalid_provides "InvalidDependency<DiskStorage, ?keel::Provides<const Storage> ?>")
keel_compile_fail(undeclared_provider "UndeclaredProvider<Server, ?Metrics, ?PrometheusMetrics>")
keel_compile_fail(undeclared_optional_provider "UndeclaredProvider<Server, ?Metrics, ?PrometheusMetrics>")
keel_compile_fail(undeclared_init_after_provider "UndeclaredProvider<Server, ?Metrics, ?PrometheusMetrics>")
keel_compile_fail(root_get_ambiguous "AmbiguousInterface<Storage, ?keel::detail::TypeList<DiskStorage, ?MemoryStorage ?> ?>")
keel_compile_fail(root_get_not_provided "NotInApp<Storage>")
keel_compile_fail(run_ambiguous_interface "RunTargetAmbiguous<Shell, ?keel::detail::TypeList<Terminal, ?Console ?> ?>")
keel_compile_fail(run_inherited_member "RunTargetNotComposed<Loop, ?int")
keel_compile_fail(ambiguous_dependency_no_cycle "AmbiguousDependency<A, ?Storage, ?keel::detail::TypeList<B, ?C ?> ?>")
keel_compile_fail(undeclared_subclass "UndeclaredSubclass<Service, ?Clock, ?FakeClock>")
keel_compile_fail(root_get_inherited_ambiguous "AmbiguousInterface<Clock, ?keel::detail::TypeList<Clock, ?FakeClock ?> ?>")
