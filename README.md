# keel

**Structure for C++ applications: components with declared dependencies, a composition
checked by the compiler, and startup and shutdown order computed for you.**

keel is a single C++20 header (about 2,500 lines, standard library only). You split your
application into *components* (the logger, the config, the database pool, the HTTP
server, the device manager), and each component says in its class declaration which other
components it uses. You list the components of your application in one place. keel then:

- **refuses to compile** if a dependency is missing, ambiguous or cyclic: one error per
  mistake, at each place that needs it, naming the types involved;
- lets each component reach **only the dependencies it declared**;
- **starts components in dependency order and stops them in reverse**, so nothing is used
  before it exists or after it's gone, and rolls back cleanly when startup fails.

It replaces the *god class*: the `Application` object that owns every subsystem, wires them
in one constructor, and whose member order silently decides what is destroyed first. Doing
it by hand, with ordered members or `unique_ptr`s in `main()`, works until it doesn't: the
order is written instead of derived, and nothing checks it.

It's a structure for the program that owns `main()`: dependencies on the class line, one
checked list of components, and a startup and shutdown order derived from them. It isn't a
dependency-injection container; [Why keel](docs/design.md) explains the difference and
compares it with existing C++ DI libraries.

## A first example

```cpp
#include "keel.hpp"

#include <cstdio>
#include <string>

// A component with no dependencies. It receives argc/argv in its constructor.
class Config : public keel::Component<> {
public:
    Config(int argc, char** argv) : name_(argc > 1 ? argv[1] : "world") {}
    const std::string& name() const { return name_; }

private:
    std::string name_;
};

// Greeter uses Config: the dependency is listed in its base class.
class Greeter : public keel::Component<Config> {
public:
    void greet() const { std::printf("hello, %s\n", config_->name().c_str()); }

private:
    void init() override { config_ = &getComponent<Config>(); }  // dependencies are ready from init() on
    const Config* config_ = nullptr;
};

int main(int argc, char** argv) {
    // The composition root: every component, in any order. Checked at compile time.
    auto app = keel::compose<Greeter, Config>(argc, argv);
    app.run(&Greeter::greet);  // init, start, greet(), stop, deinit
}
```

```sh
$ c++ -std=c++20 -I path/to/keel hello.cpp -o hello && ./hello Ada
hello, Ada
```

Remove `Config` from `compose<...>` and the program doesn't compile:

```
error: static assertion failed due to requirement 'dependencyComposed<Greeter, Config>': keel: missing
dependency: the Component in MissingDependency<Component, Dependency> needs Dependency, and no composed
component is or provides it.
...
hello.cpp:28:16: note: in instantiation of template class 'keel::App<Greeter>' requested here
```

`MissingDependency<Greeter, Config>`: Greeter needs Config. The requirement names the
types, and the last note is your line. Call `getComponent<Database>()` in `Greeter` without
listing it, and that doesn't compile either. The [getting started guide](docs/getting-started.md)
walks through a larger program, and [a whole application](docs/walkthrough.md) puts the
features together.

## Features

**Every dependency is declared.** Required, optional, or ordering only; the accessor must
match the declaration.

```cpp
class HttpServer : public keel::Component<Router, Logger, keel::Optional<Metrics>, keel::InitAfter<Migrations>> {
    void init() override {
        router_ = &getComponent<Router>();     // Router&: always there
        metrics_ = findComponent<Metrics>();   // Metrics*: nullptr if not composed or disabled
    }
    Router* router_ = nullptr;
    Metrics* metrics_ = nullptr;
};
```

**The order is derived, and the lifecycle is staged.** `init()` and `start()` in dependency
order, `stop()` and `deinit()` in reverse. A failed startup rolls back what was done and
rethrows your exception. Shutdown can't throw.

```cpp
class Database : public keel::Component<Config, Logger> {
    void init() override;             // connect; throw to abort startup
    void start() override;            // after every component's init()
    void stop() noexcept override;    // after everything that uses the Database has stopped
    void deinit() noexcept override;  // disconnect; the Logger is still there
};
```

**Interfaces and collections.** Depend on an interface that one component provides, or on
every provider of it. Two providers where one is needed is a compile error.

```cpp
class DiskStorage : public keel::Component<keel::Provides<Storage>>, public Storage { /* ... */ };
class Service : public keel::Component<Storage> { /* ... */ };                      // the one Storage
class HealthEndpoint : public keel::Component<keel::All<HealthCheck>> { /* ... */ };  // every HealthCheck
```

And more:

| Feature | In one line |
|---|---|
| [Decisions at runtime](docs/runtime-decisions.md) | a component can opt out (`enabled()`), and configuration can choose between implementations (`keel::Alternative<I>`) |
| [Bundles and guards](docs/composition.md) | name groups of components, compose variants, check every variant on every build (`keel::requireValid`) |
| [Async startup](docs/async.md) | a slow `init()` or `start()` runs on a thread of its own (`keel::AsyncInit`, `keel::AsyncStart`), opt-in |
| [Introspection](docs/inspection-and-testing.md) | the init order at compile time, timings of every lifecycle call, the graph for Graphviz |

**Compile time.** keel's test with 150 components (up to five dependencies each) compiles
in about 1 s on top of the standard headers (Apple clang 17, `-O0`, Apple M1).

## Documentation

| Document | What's in it |
|---|---|
| [Getting started](docs/getting-started.md) | installation, a complete program, components, `getComponent`, the composition root |
| [A whole application](docs/walkthrough.md) | one program that uses the main features, step by step |
| [Dependencies and interfaces](docs/dependencies.md) | required, `Optional`, `InitAfter`, `Provides`, `All`, `Optional<All>` |
| [Lifecycle](docs/lifecycle.md) | `init` / `start` / `stop` / `deinit`, guarantees, failures and rollback, `run` |
| [Composing an application](docs/composition.md) | `compose`, `App`, constructor arguments, `Bundle`, several compositions |
| [Decisions at runtime](docs/runtime-decisions.md) | `enabled()`, `Alternative`, turning a feature off |
| [Async startup](docs/async.md) | `AsyncInit`, `AsyncStart`, scheduling, thread safety, `setAsync` |
| [Inspecting and testing](docs/inspection-and-testing.md) | `requireValid`, `isValid`, init order, timings, the DOT graph, testing with fakes |
| [Errors](docs/errors.md) | every compile-time diagnostic with its fix, and every runtime `keel::Error` |
| [Patterns](docs/patterns.md) | shutdown on a signal, several instances of a class, role interfaces, null objects, fakes |
| [Reference](docs/reference.md) | API, guarantees, platform notes, limitations |
| [Why keel](docs/design.md) | the problem, what keel is not, other C++ libraries, the core rules |

## Examples

In [`examples/`](examples/). Each is a small program with build and run instructions at the
top; the CMake build compiles all of them and runs them as tests.

| File | Shows |
|---|---|
| [`compose.cpp`](examples/compose.cpp) | components, `getComponent`, `Optional`, the composition root |
| [`interfaces.cpp`](examples/interfaces.cpp) | `Provides`, `All`, `Optional<All>`, build variants, a test with fakes |
| [`lifecycle.cpp`](examples/lifecycle.cpp) | `init`/`start`/`stop`/`deinit`, `InitAfter`, `run`, rollback of a failed start |
| [`runtime_choices.cpp`](examples/runtime_choices.cpp) | `enabled()`, `Alternative`, `keel::run` with a member function |
| [`async.cpp`](examples/async.cpp) | `AsyncInit`, `AsyncStart`, `setAsync(false)` |
| [`introspection.cpp`](examples/introspection.cpp) | `initOrder()`, `timings()`, `toDot()` |
| [`bundles.cpp`](examples/bundles.cpp) | `Bundle`, several compositions, `requireValid`, `isValid` |
| [`telemetryd.cpp`](examples/telemetryd.cpp) | a small telemetry agent: seven components, no god class |
| [`compile_errors.cpp`](examples/compile_errors.cpp) | every compile-time check: build with `-DKEEL_ERROR=1` … `34` |

## Requirements

- A C++20 compiler and standard library. No other dependencies. No RTTI needed;
  exceptions are used for errors.
- Tested with **Apple clang 17** on macOS. Not yet tried with GCC or MSVC.
- MSVC's default constexpr step limit is about ten times lower than Clang's, so large
  compositions (around a hundred components or more) will likely need
  `/constexpr:steps1000000` there. More in [platform notes](docs/reference.md#requirements-and-platform-notes).

## Using keel

With CMake:

```cmake
add_subdirectory(keel)  # or FetchContent
target_link_libraries(app PRIVATE keel::keel)
```

Or copy [`keel.hpp`](keel.hpp) into your project and include it.

**Threads.** keel runs `AsyncInit`/`AsyncStart` components on threads of their own, so it
uses `std::thread`. The `keel::keel` target links `Threads::Threads`; without CMake, compile
and link with `-pthread` on Linux. Async is opt-in, but once a component uses it, unrelated
components may run at the same time, so dependencies they share must be thread-safe (see
[Async startup](docs/async.md#what-async-requires-of-you)).

## Building the tests and examples

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

The tests include compile-fail tests: programs that must not compile, each checked for the
expected diagnostic and for exactly one error. `-DKEEL_BUILD_TESTS=OFF` and
`-DKEEL_BUILD_EXAMPLES=OFF` turn the two parts off (both are on when keel is the top-level
project, off when it is added as a subdirectory).

## License

[MIT](LICENSE)

## AI Usage Disclaimer

Portions of this repository - including code, comments, and documentation - were generated or assisted by artificial intelligence tools.

All AI-generated outputs have been manually reviewed, tested, and refined by the project maintainers to ensure accuracy, security, and compliance with the repository's licensing terms.
