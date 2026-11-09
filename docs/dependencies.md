# Dependencies and interfaces

[keel](../README.md) › docs › Dependencies and interfaces

What a component **needs** goes in its `keel::Component<...>` list, and only that. What it
**is** goes in its base classes: the interfaces it implements (`keel::Provides<I>`,
`keel::Alternative<I>`) and how it runs (`keel::Conditional`, `keel::AsyncInit`,
`keel::AsyncStart`):

| You write | Meaning | Access |
|---|---|---|
| `T` | **Required.** `T` must be composed; it comes first. | `getComponent<T>()` returns `T&` |
| `keel::Optional<T>` | **Optional.** Used if composed; it comes first if so. | `findComponent<T>()` returns `T*`, `nullptr` if absent or disabled |
| `keel::After<T>` | **Ordering only.** Come after `T` if `T` is composed. | none |
| `keel::All<I>` | **Every** provider of `I`, **at least one**. | `getAll<I>()` returns `std::vector<I*>`, never empty |
| `keel::Optional<keel::All<I>>` | Every provider of `I`, **possibly none**. | `getAll<I>()`, possibly empty |

| You derive from | Meaning | Effect |
|---|---|---|
| `public keel::Provides<I>` | This component **implements the interface `I`**. | others can depend on `I` |
| `public keel::Alternative<I>` | **One of several** implementations of `I`, chosen at runtime (with `keel::Conditional`). | others depend on plain `I`; see [Decisions at runtime](runtime-decisions.md#keelalternativei-implementations-that-are-components-of-their-own) |
| `public keel::Conditional` | Configuration may turn it off. | see [Decisions at runtime](runtime-decisions.md#keelconditional-a-component-that-may-be-off) |
| `public keel::AsyncInit`, `public keel::AsyncStart` | Run this component's `init()` / `start()` on a thread of its own. | see [Async startup](async.md) |

"Comes first" means constructed and initialized before this component, and deinitialized
and destroyed after it.

**One entry per type.** `T`, `Optional<T>`, `After<T>`, `All<T>` and
`Optional<All<T>>` all name `T`, so two of them in one list is an error. Each entry has
exactly one accessor, and the wrong one is a compile error that names the right one:
`getComponent<T>()` on an `Optional<T>` says to use `findComponent<T>()`.

## Required: `T`

```cpp
class Router : public keel::Component<Database, Logger> {
    void init() override { db_ = &getComponent<Database>(); }
    Database* db_ = nullptr;
};
```

`Database` must be composed (else `MissingDependency<Router, Database>`) and is
initialized before `Router`. It can't be a component that configuration may turn off (a
[`keel::Conditional`](runtime-decisions.md#keelconditional-a-component-that-may-be-off)):
that is a compile error, `RequiredConditional<Router, Database>`. A required dependency is
always there.

## Optional: `keel::Optional<T>`

For a dependency the component can live without: metrics, tracing, a cache.

```cpp
class HttpServer : public keel::Component<Router, keel::Optional<Metrics>> {
    void init() override {
        metrics_ = findComponent<Metrics>();  // nullptr if Metrics isn't composed
    }
    void onRequest() {
        if (metrics_ != nullptr) metrics_->count("request");
    }
    Metrics* metrics_ = nullptr;
};
```

If `Metrics` is composed, it comes before `HttpServer`. If it isn't, or if it is a
`keel::Conditional` that is disabled at runtime, `findComponent` returns `nullptr` and the
application runs without it.

## Ordering only: `keel::After<T>`

For "must come after, but doesn't use it". The server must not start before the database
migrations have run, but it never calls the migrations component:

```cpp
class Migrations : public keel::Component<Database> {
    void init() override;  // runs the schema migrations
};

class HttpServer : public keel::Component<Router, keel::After<Migrations>> {
    void start() override;  // listen: only after Migrations::init() has run
};
```

`After<T>` gives no access (`getComponent<Migrations>()` doesn't compile; the error
says to replace `After<Migrations>` with `Migrations`). It orders construction and
destruction too. If `T` isn't composed, nothing happens, like systemd's `After=`. For an
interface `I`, `After<I>` comes after every provider of `I`.

## Interfaces: `keel::Provides<I>`

A component can depend on an **interface** instead of a concrete class. The interface is
a plain class, not a component. A component that implements it derives publicly from
`keel::Provides<I>` and from `I`:

```cpp
struct Storage {  // the interface: plain C++
    virtual ~Storage() = default;
    virtual void put(const std::string& key, const std::string& value) = 0;
};

class DiskStorage : public keel::Component<Config>, public keel::Provides<Storage>, public Storage {
public:
    void put(const std::string& key, const std::string& value) override;
};

class Service : public keel::Component<Storage> {  // depends on the interface
    void init() override { storage_ = &getComponent<Storage>(); }
    Storage* storage_ = nullptr;
};

auto app = keel::compose<Service, DiskStorage, Config>();
```

- A dependency on plain `Storage` (required or `Optional`) means "**the** storage": the
  composition must have one provider. With two, it is `AmbiguousDependency`, unless the
  providers are [alternatives](runtime-decisions.md#keelalternativei-implementations-that-are-components-of-their-own)
  or the dependent asks for a specific one through a
  [role interface](patterns.md#picking-one-of-several-providers-role-interfaces).
- Several providers of an interface are fine in themselves. A component that wants all of
  them lists `keel::All<I>` (below). The error appears only where one `I` is needed.
- A component may provide several interfaces: one `keel::Provides<>` base per interface.
- It must derive publicly and unambiguously from each one (else
  `NotDerivedFromInterface<C, I>`), and from each `keel::Provides<I>` publicly and once
  (else `InaccessibleBase<C, keel::Provides<I>>`; `class` bases are private by default).
- `keel::Provides<I>` is not a list entry: listing it (or `keel::Alternative<I>`) in
  `keel::Component<...>` is `InvalidDependency<C, keel::Provides<I>>`. The list says what a
  component needs, its bases what it is.
- keel finds out what a component provides by asking: "does `C` provide `I`?", for each
  `I` that something refers to (a dependency of a composed component, or the root's
  `getComponent<I>()` or `run(&I::f)`). C++20 can't list a class's base classes. So a
  `keel::Provides<I>` or `keel::Alternative<I>` that nothing refers to has no effect: it
  isn't checked (except that an alternative must be a `keel::Conditional`), and
  [`App::toDot()`](inspection-and-testing.md) doesn't show it.

This is how you swap implementations without touching the components that use them:
production composes `DiskStorage`, a test composes `FakeStorage` (see
[Testing](inspection-and-testing.md#testing-components)).

### Dependencies match by exact type

A dependency on `T` resolves to the component `T`, or to the one that derives from
`Provides<T>` (or `Alternative<T>`). Nothing else: keel doesn't look for a class that
happens to derive from `T`. When one does, that is almost always a forgotten
`Provides<T>`, so keel reports it:

```cpp
class PrometheusMetrics : public keel::Component<>, public Metrics {};  // no Provides<Metrics>
class Server : public keel::Component<Metrics> {};

keel::compose<Server, PrometheusMetrics>();  // UndeclaredProvider<Server, Metrics, PrometheusMetrics>
```

The same applies to `Optional<T>` and `After<T>`, which would otherwise silently find
nothing.

### Substituting a subclass for a component

To replace a component by a subclass (a fake clock in a test), let the component provide
itself. Its subclasses inherit the `Provides<>`:

```cpp
class Clock : public keel::Component<>, public keel::Provides<Clock> {
public:
    virtual long now() const;
};
class FakeClock : public Clock {
public:
    long now() const override { return 42; }
};

class Service : public keel::Component<Clock> { /* ... */ };

auto test = keel::compose<Service, FakeClock>();  // Service's Clock is the FakeClock
```

Without `Provides<Clock>` in `Clock`, composing `FakeClock` in its place is
`UndeclaredProvider<Service, Clock, FakeClock>`. With it, composing both `Clock` and
`FakeClock` is ambiguous, as for any interface.

## Collections: `keel::All<I>` and `keel::Optional<keel::All<I>>`

For plug-in style lists: routes, health checks, sensors, subcommands, per-frame systems.

```cpp
struct HealthCheck {
    virtual ~HealthCheck() = default;
    virtual bool healthy() const = 0;
};

class Database : public keel::Component<>, public keel::Provides<HealthCheck>, public HealthCheck { /* ... */ };
class Cache : public keel::Component<>, public keel::Provides<HealthCheck>, public HealthCheck { /* ... */ };

class HealthEndpoint : public keel::Component<keel::All<HealthCheck>> {
    void init() override { checks_ = getAll<HealthCheck>(); }
    bool allHealthy() const {
        for (const HealthCheck* check : checks_) {
            if (!check->healthy()) return false;
        }
        return true;
    }
    std::vector<HealthCheck*> checks_;
};
```

- **`All<I>` means "all of them, and at least one".** A health endpoint with nothing to
  check, or sensor fusion with no sensors, is a composition mistake, so no provider is a
  compile error: `MissingProvider<HealthEndpoint, HealthCheck>`.
- **`Optional<All<I>>` means "all of them, possibly none"**, for plugins that may or may
  not be installed:

  ```cpp
  class PluginMenu : public keel::Component<keel::Optional<keel::All<Plugin>>> {
      void init() override {
          for (Plugin* plugin : getAll<Plugin>()) addEntry(*plugin);  // may be empty
      }
      void addEntry(Plugin&);
  };
  ```

- `getAll<I>()` returns the enabled providers **in init order**: each after the ones it
  depends on. A game loop that updates `All<System>` updates input before physics before
  rendering, if that's how they depend on each other. It returns a new vector per call:
  call it in `init()` and keep it.
- Every provider comes before the component that lists the collection.
- **A component is not part of its own list.** A composite (a `HealthCheck` that checks
  all the others) derives from `Provides<HealthCheck>`, lists `All<HealthCheck>`, and gets
  the others; it is ordered after them.
- Providers that configuration turned off (a disabled
  [`keel::Conditional`](runtime-decisions.md#keelconditional-a-component-that-may-be-off))
  are left out. So that an `All<I>` is never empty, at least one provider must not be
  conditional, else it is a compile error: `OnlyConditionalProviders<HealthEndpoint,
  HealthCheck, TypeList<...>>`. `Optional<All<I>>` has no such rule.
- **Nothing is left out silently.** A composed class that derives from `I` without
  providing it would be missing from the list, so it is a compile error:
  `UndeclaredProvider<HealthEndpoint, HealthCheck, Cache>`.
- `All<Optional<I>>` is `InvalidDependency`, with a hint to write `Optional<All<I>>`.
