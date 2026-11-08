# Each example is a program that builds and runs on its own; ctest runs each one (some
# with arguments) and checks its exit code and output. compile_errors.cpp builds as a
# program without KEEL_ERROR, and once per KEEL_ERROR value as a compile-fail test, with the
# runner of keel's own compile-fail tests (tests/compile_fail.cmake).

function(keel_example name)
    add_executable(keel_example_${name} ${name}.cpp)
    set_target_properties(keel_example_${name} PROPERTIES OUTPUT_NAME ${name})
    target_link_libraries(keel_example_${name} PRIVATE keel::keel)
    if(MSVC)
        target_compile_options(keel_example_${name} PRIVATE /W4 /permissive- /constexpr:steps1000000)
    else()
        target_compile_options(keel_example_${name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# keel_example_run(<example> <test name> <expected exit code> <output regex> [args...])
# Checks the exit code and that the output matches the regex (run_example.cmake).
function(keel_example_run name test exit pattern)
    string(REPLACE ";" "|" args "${ARGN}")
    add_test(NAME example.${test}
             COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:keel_example_${name}> "-DARGS=${args}" -DEXIT=${exit}
                     "-DPATTERN=${pattern}" -P ${CMAKE_CURRENT_SOURCE_DIR}/run_example.cmake)
    set_tests_properties(example.${test} PROPERTIES TIMEOUT 60)
endfunction()

keel_example(compose)
keel_example_run(compose compose 0 "hello, Ada \\(tick 2\\).*without a Clock.*hello, Ada\n" Ada)

keel_example(interfaces)
keel_example_run(interfaces interfaces 0 "using disk storage.*health network  ok.*fake recorded: answer@42")

keel_example(lifecycle)
keel_example_run(lifecycle lifecycle 0 "run returned 3.*step by step.*deinit Logger")
keel_example_run(lifecycle lifecycle.fail 1 "start  Database\n  stop   Database\n  deinit HttpServer.*startup failed: cannot listen on port 8080" --fail)

keel_example(runtime_choices)
keel_example_run(runtime_choices runtime_choices 0 "^serving with the memory cache and no compression\n$")
keel_example_run(runtime_choices runtime_choices.redis 0 "serving with the redis cache and zip compression\n  telemetry: request" --redis --zip --telemetry)
keel_example_run(runtime_choices runtime_choices.both 1 "keel: Service uses Cache: several of its alternatives are enabled" --redis --memory)

keel_example(async)
keel_example_run(async async 0 "init   Cache      \\(async thread\\).*init   HttpServer \\(main thread\\).*start  Feed       \\(async thread\\)")
keel_example_run(async async.sync 0 "init   Cache      \\(main thread\\).*start  Feed       \\(main thread\\)" --sync)

keel_example(introspection)
keel_example_run(introspection introspection 0 "init order: Config Logger Database Metrics Cache HealthEndpoint.*deinit Config")
keel_example_run(introspection introspection.dot 0 "^digraph keel \\{.*n0 -> n2 \\[style=bold, label=\"HealthCheck\"\\];" dot)

keel_example(bundles)
keel_example_run(bundles bundles 0 "connected to postgres.*serving")
keel_example_run(bundles bundles.bench 0 "connected to in-memory.*serving" --bench)

keel_example(telemetryd)
keel_example_run(telemetryd telemetryd 0 "\\[info\\] export to collector.local:9000: mean=[0-9.]+\n\\[info\\] export to collector.local:9000: mean=[0-9.]+\n\\[info\\] export to collector.local:9000: mean=[0-9.]+\n.*logger down" --rounds 3)
keel_example_run(telemetryd telemetryd.bad_rounds 2 "--rounds needs a positive number" --rounds x)
keel_example_run(telemetryd telemetryd.unknown_flag 2 "unknown argument: --verbose" --verbose --rounds 1)

keel_example(compile_errors)
keel_example_run(compile_errors compile_errors 0 "^$")

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set(count_errors ON)
else()
    set(count_errors OFF)
endif()

# keel_example_error(<KEEL_ERROR value> <regex the compiler output must match>)
# Exactly one error per case (GCC/Clang), as keel promises.
function(keel_example_error number pattern)
    set(target keel_example_error_${number})
    add_library(${target} OBJECT EXCLUDE_FROM_ALL compile_errors.cpp)
    target_compile_definitions(${target} PRIVATE KEEL_ERROR=${number})
    target_link_libraries(${target} PRIVATE keel::keel)
    if(MSVC)
        target_compile_options(${target} PRIVATE /constexpr:steps1000000)
    endif()
    add_test(NAME example.compile_errors.${number}
             COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -DTARGET=${target} -DCONFIG=$<CONFIG>
                     "-DPATTERN=${pattern}" -DCOUNT_ERRORS=${count_errors} -DERRORS=1
                     -P ${PROJECT_SOURCE_DIR}/tests/compile_fail.cmake)
    set_tests_properties(example.compile_errors.${number} PROPERTIES RESOURCE_LOCK keel_build_tree)
endfunction()

# MSVC prints template arguments without a space after the comma: ", ?" matches both.
keel_example_error(1 "NotAComponent<Settings>")
keel_example_error(2 "DuplicateComponent<Logger>")
keel_example_error(3 "AbstractComponent<Sink>")
keel_example_error(4 "EmptyComposition<>")
keel_example_error(5 "DuplicateBundle<keel::Bundle<Config, ?Logger ?> ?>")
keel_example_error(6 "MissingDependency<Server, ?Logger>")
keel_example_error(7 "DependencyCycle<keel::detail::TypeList<P, ?Q, ?R> ?>")
keel_example_error(8 "InvalidDependency<Server, ?keel::Optional<const Metrics> ?>")
keel_example_error(9 "BundleAsDependency<Server, ?keel::Bundle<Config, ?Logger ?> ?>")
keel_example_error(10 "MisplacedAsync<Database, ?keel::Optional<keel::AsyncInit> ?>")
keel_example_error(11 "a dependency is listed twice")
keel_example_error(12 "NotDerivedFromInterface<Archive, ?Storage>")
keel_example_error(13 "AmbiguousDependency<Service, ?Storage, ?keel::detail::TypeList<DiskStorage, ?MemoryStorage ?> ?>")
keel_example_error(14 "MixedProviders<Storage, ?keel::detail::TypeList<DiskStorage ?>, ?keel::detail::TypeList<S3Storage ?> ?>")
keel_example_error(15 "UndeclaredProvider<Server, ?Metrics, ?PrometheusMetrics>")
keel_example_error(16 "UndeclaredSubclass<Service, ?Clock, ?FakeClock>")
keel_example_error(17 "MissingProvider<HealthEndpoint, ?HealthCheck>")
keel_example_error(18 "LeftOutOfAll<HealthEndpoint, ?HealthCheck, ?Cache>")
keel_example_error(19 "MisnestedAll<PluginMenu, ?Plugin>")
keel_example_error(20 "getComponent<T>\\(\\) requires T to be listed in this component")
keel_example_error(21 "T is declared keel::Optional<T>; use findComponent<T>")
keel_example_error(22 "T is declared keel::InitAfter<T>, which is ordering only")
keel_example_error(23 "T is a required dependency; use getComponent<T>")
keel_example_error(24 "I is a required dependency \\(one provider\\); use getComponent<I>")
keel_example_error(25 "keel::AsyncInit and keel::AsyncStart are markers, not dependencies")
keel_example_error(26 "exception specification")
keel_example_error(27 "NotInApp<Storage>")
keel_example_error(28 "AmbiguousInterface<Storage, ?keel::detail::TypeList<DiskStorage, ?MemoryStorage ?> ?>")
keel_example_error(29 "RunTargetNotComposed<Cli, ?int")
keel_example_error(30 "RunTargetAmbiguous<Shell, ?keel::detail::TypeList<Terminal, ?Console ?> ?>")
keel_example_error(31 "RunTargetNotCallable<int ?\\(.*Cli::\\*\\)\\(int\\)")
keel_example_error(32 "RunNotCallable<")
keel_example_error(33 "NotAnApp<Logger>")
keel_example_error(34 "MissingDependency<Server, ?Logger>")
keel_example_error(35 "RequiredConditional<Dashboard, ?Uplink>")
keel_example_error(36 "RequiredConditionalProvider<Service, ?Storage, ?S3Storage>")
keel_example_error(37 "OnlyConditionalProviders<HealthEndpoint, ?HealthCheck, ?keel::detail::TypeList<DiskCheck ?> ?>")
keel_example_error(38 "AlternativeNotConditional<S3Storage, ?Storage>")
keel_example_error(39 "InaccessibleConditional<Uplink>")
keel_example_error(40 "AbstractComponent<Uplink>")
keel_example_error(41 "RunTargetConditional<Uplink, ?Uplink>")
