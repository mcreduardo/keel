# Composing an application

[keel](../README.md) › docs › Composing an application

## `keel::compose<...>(args...)` and `keel::App<...>`

```cpp
auto app = keel::compose<A, B, C>(argc, argv);  // a keel::App<A, B, C>
keel::App<A, B, C> same(argc, argv);            // the same thing
```

The composition is checked when the `App` type is instantiated: types that aren't
components, duplicates, missing and ambiguous dependencies, cycles, and the rest of
[the compile-time checks](errors.md#at-compile-time). Then keel derives the order from the
dependency graph:

- **Each component comes after everything it depends on**: required dependencies,
  `Optional<T>` and `InitAfter<T>` when `T` is composed, every provider of an `All<I>`, and,
  for a dependency on an interface `I` with `Alternative<I>` providers, every alternative.
- **Components that don't depend on each other keep the order of the list.** Without
  [async components](async.md), the order is fully determined by the graph and the list,
  and the same on every run.

The order applies to everything: construction, `init()` and `start()` follow it;
`stop()`, `deinit()` and destruction run in reverse.

- **Components are stored inside the `App` object**, not heap-allocated (the registry has
  its own small tables).
- **The `App` can't be copied or moved**: components hold pointers to each other. Create
  it where it lives, usually in `main()`.
- **The composition is fixed at compile time.** Configuration can turn components off or
  choose between implementations ([Decisions at runtime](runtime-decisions.md)), but it
  can't add components.

## Constructor arguments

The arguments of `compose(...)` (or the `App` constructor) go to every component that is
constructible from them. All other components are default-constructed.

```cpp
class CliArgs : public keel::Component<> {
public:
    CliArgs(int argc, char** argv);  // receives compose()'s arguments
};
class Logger : public keel::Component<CliArgs> {};  // default-constructed

auto app = keel::compose<Logger, CliArgs>(argc, argv);
```

- **Keep constructors cheap.** Store the arguments and do the real work in `init()`.
  Dependencies aren't usable in a constructor.
- **Use distinctive parameter types.** Arguments go to *every* component that accepts
  them, so a component with a `std::string` constructor would also receive a path meant
  for `Config`. A settings struct avoids that.
- **Arguments are passed as lvalue references.** A component may copy them; it must not
  keep a reference to an argument that is a temporary in the `compose(...)` call.
- **A near miss falls back to the default constructor, silently.** If a component's
  constructor takes only some of the arguments, is ambiguous for them, or needs one by
  value that is move-only (arguments are passed as lvalues), keel default-constructs it,
  if it can. A component that can't be constructed either way is a compile error.

### Settings for several components

A component is constructed from *all* the arguments or default-constructed, so a component
that takes one of two arguments gets neither. Pass one settings struct, and let each
component take its part:

```cpp
struct Settings {
    struct Http {
        int port = 8080;
    } http;
    struct Database {
        std::string url = "postgres://localhost/app";
        int poolSize = 4;
    } database;
};

class Database : public keel::Component<> {
public:
    explicit Database(const Settings& settings) : settings_(settings.database) {}

private:
    Settings::Database settings_;
};

class HttpServer : public keel::Component<Database> {
public:
    explicit HttpServer(const Settings& settings) : settings_(settings.http) {}

private:
    Settings::Http settings_;  // a copy
};

int main(int argc, char** argv) {
    const Settings settings = parseSettings(argc, argv);
    auto app = keel::compose<HttpServer, Database>(settings);
    app.run(waitForShutdownSignal);
}
```

## Accessing components from the root

`app.getComponent<T>()` returns a component, or the one provider of an interface:

```cpp
app.getComponent<HttpServer>();  // a component of the App
app.getComponent<Storage>();     // the one component that lists keel::Provides<Storage>
```

- It works **before `init()`** too, so the root can configure a component before startup.
  After `init()`, a [`keel::Conditional`](runtime-decisions.md#keelconditional-a-component-that-may-be-off)
  component that is disabled throws `keel::Error`.
- An interface whose providers are `keel::Alternative<I>`s is chosen by `init()`, so asking
  for it before `init()` throws `keel::Error`. After `init()` you get the enabled one.
- A type the `App` doesn't have is a compile error (`NotInApp<T>`), and so is an interface
  with two providers (`AmbiguousInterface<I, TypeList<...>>`, or `MixedProviders<...>` when
  some are `Provides<I>` and others `Alternative<I>`).

## Grouping: `keel::Bundle<...>`

Name groups of components and reuse them:

```cpp
using Core = keel::Bundle<CliArgs, Config, Logger>;
using Persistence = keel::Bundle<Database, Migrations, Cache>;
using Http = keel::Bundle<Router, HttpServer>;

auto app = keel::compose<Core, Persistence, Http>(argc, argv);
```

- A bundle is **expanded in place**, so `compose<Core, Http>` is
  `compose<CliArgs, Config, Logger, Router, HttpServer>`. Bundles may contain bundles.
- **Each component and each bundle once.** A component that two bundles contain is
  `DuplicateComponent<T>`; a bundle reached twice is `DuplicateBundle<B>`. Write
  overlapping groups as a shared bundle plus the rest.
- **A bundle is not a dependency.** `keel::Component<Core>` is `BundleAsDependency`: a
  component lists the components it uses, each on its own.
- `keel::App<Core, Router>` and `keel::App<Config, Logger, Router>` have the same
  components but are different types.

## Composition guards: `requireValid` and `isValid`

```cpp
static_assert(keel::requireValid<Core, Persistence, Http>);    // production
static_assert(keel::requireValid<Core, FakeDatabase, Http>);   // the test variant
static_assert(!keel::isValid<Core, Http>);                     // Router needs a Database
```

`requireValid` is true or the compile error `compose` would give. `isValid` is a plain
`bool`. Put guards next to the compositions, and every variant is checked on every build,
including variants that no binary of the current build uses. More in
[Inspecting and testing](inspection-and-testing.md#checking-compositions-requirevalid-and-isvalid).

## Several compositions in one program

Build variants (production, simulator, test) are different compositions of mostly the
same components, chosen in the composition root:

```cpp
using Device = keel::App<Core, SerialBus, Sensors, Recorder>;
using Bench = keel::App<Core, SimulatedBus, Sensors, Recorder>;

int main(int argc, char** argv) {
    return simulate(argc, argv) ? keel::run<Bench>(&Recorder::record, argc, argv)
                                : keel::run<Device>(&Recorder::record, argc, argv);
}
```

`keel::run<AppType>(fn, args...)` constructs the App from `args`, calls `run(fn)` on it
([Lifecycle](lifecycle.md#driving-the-lifecycle)) and returns what `fn` returns, by value.
Each composition is checked on its own, and only the ones you instantiate (or guard)
are checked.

A command-line tool with subcommands works the same way. Each subcommand gets its own
composition, so `tool status` doesn't pay for an HTTPS client it never uses:

```cpp
using StatusApp = keel::App<Core, LocalCache, StatusCommand>;
using SyncApp = keel::App<Core, LocalCache, HttpClient, SyncCommand>;

if (command == "status") return keel::run<StatusApp>(&StatusCommand::execute, argc, argv);
if (command == "sync") return keel::run<SyncApp>(&SyncCommand::execute, argc, argv);
```

A separate composition is also how you leave a feature out of the binary. To switch it off
by configuration instead, see [Decisions at runtime](runtime-decisions.md).

## Large compositions

The init order is computed at compile time, in O(n²) for n components. keel's tests
compose 150 components within Clang's default limits. MSVC's default constexpr step limit
is about ten times lower than Clang's, so compositions of around a hundred components or
more will likely need `/constexpr:steps1000000` there (keel's own tests set it).
