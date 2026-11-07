# Getting started

[keel](../README.md) › docs › Getting started

## Installation

keel is one header, `keel.hpp`. It needs C++20 and nothing besides the standard library.

**With CMake**, add the repository as a subdirectory (or fetch it) and link the target:

```cmake
add_subdirectory(keel)  # or FetchContent_Declare(keel GIT_REPOSITORY https://github.com/mcreduardo/keel.git GIT_TAG main)
target_link_libraries(app PRIVATE keel::keel)
```

`keel::keel` carries the include path, C++20 and the threads library.

**Without CMake**, copy `keel.hpp` into your project and include it:

```sh
c++ -std=c++20 -I path/to/keel main.cpp -o app            # macOS
g++ -std=c++20 -I path/to/keel main.cpp -o app -pthread   # Linux: -pthread is needed
```

keel runs components on threads of their own when they ask for it
([async startup](async.md)), so on Linux it needs `-pthread`.

## A complete program

Three components: `Config` reads the command line, `Logger` uses `Config`, and `Greeter`
uses both.

```cpp
#include "keel.hpp"

#include <cstdio>
#include <exception>
#include <string>

// A component with no dependencies. It receives argc/argv in its constructor.
class Config : public keel::Component<> {
public:
    Config(int argc, char** argv) : name_(argc > 1 ? argv[1] : "world") {}
    const std::string& name() const { return name_; }

private:
    std::string name_;
};

// Logger uses Config: listed in its base class.
class Logger : public keel::Component<Config> {
public:
    void info(const std::string& message) const { std::printf("[info] %s\n", message.c_str()); }

private:
    void init() override { info("logger ready"); }
    void deinit() noexcept override { info("logger closing"); }
};

// Greeter uses Config and Logger.
class Greeter : public keel::Component<Config, Logger> {
public:
    int greet() {
        logger_->info("hello, " + config_->name());
        return 0;
    }

private:
    void init() override {
        // Dependencies are usable from init() on. Fetch them once and keep them.
        config_ = &getComponent<Config>();
        logger_ = &getComponent<Logger>();
    }

    const Config* config_ = nullptr;
    const Logger* logger_ = nullptr;
};

// The composition: every component of the application, in any order.
using HelloApp = keel::App<Greeter, Logger, Config>;

int main(int argc, char** argv) try {
    // Construct, init, start, call Greeter::greet, stop, deinit.
    return keel::run<HelloApp>(&Greeter::greet, argc, argv);
} catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
}
```

```sh
$ c++ -std=c++20 -I path/to/keel hello.cpp -o hello && ./hello Ada
[info] logger ready
[info] hello, Ada
[info] logger closing
```

Things to notice:

- **The order in the list doesn't matter.** `Greeter` comes first in `App<Greeter, Logger,
  Config>`, but keel constructs and initializes `Config`, then `Logger`, then `Greeter`,
  and shuts down in reverse.
- **Undeclared dependencies don't compile.** If `Greeter` called `getComponent<Database>()`
  without listing `Database`, that line would be a compile error.
- **Missing components don't compile.** Remove `Logger` from the list and the error is
  `MissingDependency<Greeter, Logger>`.
- **`deinit()` is `noexcept`**, and so is `stop()`. Like a destructor, shutdown has nobody
  to report a failure to; an override without `noexcept` doesn't compile.

## Components

A **component** is an object that:

- **lives as long as the application**: created once at startup, destroyed at shutdown;
- **owns some state or resource**: a file, a socket, threads, a device handle, parsed
  configuration;
- **has a [lifecycle](lifecycle.md)**: `init()`, `start()`, `stop()`, `deinit()`;
- **exists once per application.** There is one `Logger`, not many. (For several
  instances of one class, see [Patterns](patterns.md#several-instances-of-the-same-class).)

Good components: command-line arguments, configuration, logger, database pool, cache, HTTP
server, worker pool, device manager, metrics exporter.

Not components: value types, algorithms, per-request objects, small helpers. Those live
*inside* components.

A class becomes a component by deriving from `keel::Component<...>`, which lists its
dependencies:

```cpp
class Clock : public keel::Component<> { /* ... */ };                    // uses nothing
class Logger : public keel::Component<Config> { /* ... */ };             // uses Config
class Exporter : public keel::Component<Aggregator, Logger> { /* ... */ };
```

The listed types may be **forward-declared** in the header. Their definitions are needed
where you call `getComponent<T>()` (typically the `.cpp`) and where the application is
composed:

```cpp
// exporter.hpp
#include "keel.hpp"

class Aggregator;
class Logger;

class Exporter : public keel::Component<Aggregator, Logger> {
    void init() override;  // defined in exporter.cpp, which includes aggregator.hpp and logger.hpp
};
```

## Getting a dependency

Inside a component, `getComponent<T>()` returns a `T&`. It compiles **only if `T` is in the
component's list**:

```cpp
class Exporter : public keel::Component<Aggregator, Logger> {
    void init() override {
        aggregator_ = &getComponent<Aggregator>();  // OK
        logger_ = &getComponent<Logger>();          // OK
        getComponent<Database>();                   // compile error: Database isn't listed
    }
    Aggregator* aggregator_ = nullptr;
    Logger* logger_ = nullptr;
};
```

```
error: static assertion failed due to requirement 'detail::contains<Database, keel::detail::TypeList<Aggregator, Logger>>':
keel: getComponent<T>() requires T to be listed in this component's keel::Component<...>
```

Rules:

- **Dependencies are usable from `init()` on** (and from `enabled()`), not in the
  constructor. Every component is constructed first, then initialized in dependency
  order. `getComponent` from a constructor throws `keel::Error`.
- **Fetch once, keep the pointer.** A lookup is a hash-map access plus lifecycle checks
  (it doesn't allocate), so it isn't meant for a hot path. Resolve dependencies in
  `init()` and store them.
- `getComponent` is `protected`. Code outside the component can't use it to reach into
  other components.

Dependencies can also be optional, ordering-only, on an interface, or on a list of
providers: see [Dependencies and interfaces](dependencies.md).

## The composition root

The **composition root** is the one place that lists the application's components,
usually `main()`:

```cpp
auto app = keel::compose<HttpServer, Router, Database, Cache, Logger, Config>(argc, argv);
```

keel checks the composition **at compile time**: one error per mistake, at each place that
needs it, naming the types. In the requirement text, `<X, Y>` names the types
(`MissingDependency<Router, Database>`: Router needs Database), and the last note is the
line of your code that composed the App.

| Check | What fails | Error |
|---|---|---|
| Every required dependency is composed | `Router` uses `Database`, which isn't listed | `MissingDependency<Router, Database>` |
| No cycles | `A` uses `B`, `B` uses `A` | `DependencyCycle<TypeList<A, B>>` |
| Each component once | `Logger` listed twice | `DuplicateComponent<Logger>` |
| A dependency on one interface has one provider | two components provide `Cache`, and someone needs one `Cache` | `AmbiguousDependency<...>` |
| `All<I>` has a provider | a component lists `All<Sensor>`, no sensor is composed | `MissingProvider<Fusion, Sensor>` |

[Errors](errors.md) has every check, with an example and the fix for each.

Then keel computes the **init order** from the dependency graph. The order of the list only
breaks ties between components that don't depend on each other. More in
[Composing an application](composition.md).

## Mistakes the compiler can't catch

A few mistakes compile and do nothing, because a hook with the wrong signature is just
another member function:

- `bool enabled()` without `const` (or `void init(int)`) is never called. With `override`
  it is a compile error instead. **Write `override` on every hook**, and let the compiler
  remind you: `-Wsuggest-override` (Clang and GCC) warns about a hook written without it,
  and Clang's `-Winconsistent-missing-override` about a class that uses it on some hooks
  only.
- A component that already has an accessor `bool enabled() const` of its own overrides the
  hook with it: keel asks it whether the component runs. Rename the accessor.
- A constructor that takes only some of `compose()`'s arguments is skipped, and the
  component is default-constructed ([Constructor arguments](composition.md#constructor-arguments)).

## Where next

- [A whole application](walkthrough.md): one program that uses the main features.

- [Dependencies and interfaces](dependencies.md): every kind of dependency.
- [Lifecycle](lifecycle.md): what runs when, and what happens when startup fails.
- [Decisions at runtime](runtime-decisions.md): switching components by configuration.
- [Patterns](patterns.md): recipes for common situations.
- The [examples](../examples/) are small programs that build and run.
