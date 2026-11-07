# Errors

[keel](../README.md) › docs › Errors

## At compile time

Each mistake produces **one** error. The diagnostic is a class template named after the
mistake, and its template arguments name the types involved. With Clang, the first line
already says it:

```
error: static assertion failed due to requirement 'dependencyComposed<Server, Logger>': keel: missing
dependency: the Component in MissingDependency<Component, Dependency> needs Dependency, and no composed
component is or provides it.
```

and the notes below it show `keel::detail::MissingDependency<Server, Logger>`, ending with
the line of your code that composed the App. Read the message as a sentence with the
template parameters filled in: `Server` needs `Logger`.

"One error" is per mistake *and* per place that runs into it: two components that both
depend on an ambiguous interface give two errors, like an undeclared variable used twice.
A mistake in a component's own list (a duplicate, a misnested `All`) suppresses the errors
its accessors would add, so the list is the one error.

[`examples/compile_errors.cpp`](../examples/compile_errors.cpp) triggers each diagnostic
below: build it with `-DKEEL_ERROR=N` (the numbers in the headings).

### The `compose<...>` list

**`NotAComponent<T>`** (1). `T` is in `compose<...>` but doesn't derive publicly, and once,
from `keel::Component<...>`, or isn't defined at that point.

```cpp
struct Settings {};
keel::compose<Settings, Config>();  // NotAComponent<Settings>
```

Fix: derive from `keel::Component<...>`, include the definition, or remove it. (A
`keel::Bundle` is expanded only when listed as is: not `const`, not through a class
derived from it.)

**`DuplicateComponent<T>`** (2). `T` is listed more than once, counting the contents of
bundles.

```cpp
using Core = keel::Bundle<Config, Logger>;
using Diagnostics = keel::Bundle<Logger>;
keel::compose<Core, Diagnostics>();  // DuplicateComponent<Logger>
```

Fix: list it once. For overlapping groups, make the shared part its own bundle.

**`AbstractComponent<T>`** (3). `T` has a pure virtual function, so it can't be created.

Fix: compose the concrete subclass. If others depend on `T`, let `T` provide itself
(`keel::Component<keel::Provides<T>>`) so the subclass is found as `T`.

**`EmptyComposition<>`** (4). `compose<>()`, or only empty bundles. Fix: list a component.

**`DuplicateBundle<B>`** (5). The bundle `B` is reached twice: listed twice, or also inside
another bundle.

```cpp
using Core = keel::Bundle<Config, Logger>;
using Service = keel::Bundle<Core, DiskStorage>;
keel::compose<Core, Service>();  // DuplicateBundle<keel::Bundle<Config, Logger>>
```

Fix: list it once.

### A component's list

**`MissingDependency<C, D>`** (6). `C` requires `D`, and no composed component is or
provides `D`.

```cpp
class Server : public keel::Component<Logger> {};
keel::compose<Server, Config>();  // MissingDependency<Server, Logger>
```

Fix: compose `D` (or a provider of it), or make the dependency `keel::Optional<D>`.

**`DependencyCycle<TypeList<...>>`** (7). The listed components depend on each other in a
cycle. Only the components *on* a cycle are named, not those that merely depend on it.

```cpp
class Q;
class R;
class P : public keel::Component<Q> {};
class Q : public keel::Component<R> {};
class R : public keel::Component<P> {};
class Outside : public keel::Component<P> {};
keel::compose<Outside, P, Q, R>();  // DependencyCycle<TypeList<P, Q, R>>
```

Fix: break the cycle. Usually one direction is a call that should be a callback or an
interface owned by the lower component, or two components that should be one. Every kind
of entry orders (required, `Optional`, `InitAfter`, `All`), so each can close a cycle.

**`InvalidDependency<C, Entry>`** (8). An entry can never match a component: its type is
not a class, is `const` or `volatile`, is a reference, or is itself a marker.

```cpp
class Server : public keel::Component<keel::Optional<const Metrics>> {};  // InvalidDependency<Server, keel::Optional<const Metrics>>
```

Fix: name the plain class: `keel::Optional<Metrics>`. A dependency is accessed as `T&`
or `T*` already; constness is up to the code that uses it.

**`BundleAsDependency<C, B>`** (9). A component lists a bundle.

Fix: list the components it uses, each on its own. A bundle only groups components for
`compose<...>`.

**`MisplacedAsync<C, Entry>`** (10). `keel::AsyncInit` or `keel::AsyncStart` inside another
marker, such as `keel::Optional<keel::AsyncInit>`.

Fix: list the marker on its own: `keel::Component<Config, keel::AsyncInit>`.

**A dependency listed twice** (11). `T`, `Optional<T>`, `InitAfter<T>`, `All<T>` and
`Optional<All<T>>` all name `T`; two of them in one list is an error. The same goes for
`Provides<I>` and `Alternative<I>`, and for a marker listed twice.

```cpp
class Server : public keel::Component<Logger, keel::Optional<Logger>> {};
// keel: a dependency is listed twice in keel::Component<...> (T, keel::Optional<T>, ...)
```

Fix: keep the one entry that says what you mean.

### Interfaces and collections

**`NotDerivedFromInterface<C, I>`** (12). `C` lists `keel::Provides<I>` (or
`keel::Alternative<I>`), but doesn't derive from `I` publicly and unambiguously, or `I`
isn't defined at that point.

```cpp
class Archive : public keel::Component<keel::Provides<Storage>> {};  // forgot ", public Storage"
```

Fix: derive publicly from `I`.

**`AmbiguousDependency<C, I, TypeList<Providers...>>`** (13). `C` needs one `I`, and
several composed components are or provide it.

```cpp
class Service : public keel::Component<Storage> {};
keel::compose<Service, DiskStorage, MemoryStorage>();
// AmbiguousDependency<Service, Storage, TypeList<DiskStorage, MemoryStorage>>
```

Fix, depending on what you mean: compose only one; depend on the one you want by its
own type (or a [role interface](patterns.md#picking-one-of-several-providers-role-interfaces));
use every provider with `keel::All<I>`; or let configuration choose: make each one a
`keel::Alternative<I>` and enable one at runtime.

**`MixedProviders<I, TypeList<Provides...>, TypeList<Alternatives...>>`** (14). Something
needs one `I`, and some of its providers are `Provides<I>` while others are
`Alternative<I>`. Reported once per `I`, however many places need it.

Fix: make every provider an `Alternative<I>` (a do-nothing default is an alternative too),
or compose only one.

**`UndeclaredProvider<C, T, Derived>`** (15). `C` depends on `T` (required, `Optional` or
`InitAfter`), nothing composed is or provides `T`, and `Derived` derives from `T` without
listing `keel::Provides<T>`. keel matches exact types, so `Derived` would never be found.

```cpp
class PrometheusMetrics : public keel::Component<>, public Metrics {};
class Server : public keel::Component<Metrics> {};
keel::compose<Server, PrometheusMetrics>();  // UndeclaredProvider<Server, Metrics, PrometheusMetrics>
```

Fix: add `keel::Provides<T>` to `Derived`'s list, or depend on `Derived`.

**`UndeclaredSubclass<C, T, Derived>`** (16). `C` depends on the component `T`, which
isn't composed, and `Derived`, a subclass of `T`, is. A subclass can't list
`Provides<T>` itself (it would be a second `keel::Component` base).

```cpp
class Clock : public keel::Component<> {};
class FakeClock : public Clock {};
class Service : public keel::Component<Clock> {};
keel::compose<Service, FakeClock>();  // UndeclaredSubclass<Service, Clock, FakeClock>
```

Fix: compose `T` itself, or list `keel::Provides<T>` in `T`'s own list
(`Clock : keel::Component<keel::Provides<Clock>>`); its subclasses inherit it.

**`MissingProvider<C, I>`** (17). `C` lists `keel::All<I>`, which needs at least one
provider, and no composed component (other than `C`) is or provides `I`.

Fix: compose a provider, or depend on `keel::Optional<keel::All<I>>` if none is fine.

**`LeftOutOfAll<C, I, Derived>`** (18). `C` lists `keel::All<I>` (or
`Optional<All<I>>`), and `Derived` derives from `I` without providing it: it would be
silently missing from the list.

```cpp
class Cache : public keel::Component<>, public HealthCheck {};  // no Provides<HealthCheck>
class HealthEndpoint : public keel::Component<keel::All<HealthCheck>> {};
keel::compose<HealthEndpoint, Database, Cache>();  // LeftOutOfAll<HealthEndpoint, HealthCheck, Cache>
```

Fix: list `keel::Provides<I>` in `Derived` (for a subclass of a component `I`, in `I`'s own
list), or stop deriving from `I`.

**`MisnestedAll<C, I>`** (19). `keel::All<keel::Optional<I>>`. Fix: write
`keel::Optional<keel::All<I>>` for a list that may be empty.

### Inside a component

These fire where the component's code is compiled, composed or not. The message says which
accessor to use instead.

| Mistake (number) | Message (shortened) | Fix |
|---|---|---|
| `getComponent<T>()` for a `T` the list doesn't name (20) | `getComponent<T>() requires T to be listed in this component's keel::Component<...>` | list `T` |
| `getComponent<T>()` on `Optional<T>` (21) | `T is declared keel::Optional<T>; use findComponent<T>(), which may return nullptr` | `findComponent<T>()` |
| `getComponent<T>()` on `InitAfter<T>` (22) | `T is declared keel::InitAfter<T>, which is ordering only; replace it with T to use T` | list `T` instead |
| `findComponent<T>()` on a required `T` (23) | `T is a required dependency; use getComponent<T>(), which never returns nullptr` | `getComponent<T>()` |
| `findComponent<T>()` without `Optional<T>` | `findComponent<T>() requires keel::Optional<T> in this component's keel::Component<...>` | list `Optional<T>` |
| `getAll<I>()` on a required `I` (24), `Optional<I>` or `InitAfter<I>` | `I is a required dependency (one provider); use getComponent<I>(), or replace it with keel::All<I> ...` | `getComponent`/`findComponent`, or list `All<I>` |
| `getAll<I>()` without `All<I>` | `getAll<I>() requires keel::All<I> or keel::Optional<keel::All<I>> ...` | list one of them |
| `getComponent<T>()`/`findComponent<T>()` on `All<T>` | `T is declared keel::All<T> or keel::Optional<keel::All<T>>; use getAll<T>()` | `getAll<T>()` |
| `getComponent<keel::AsyncInit>()` (25) | `keel::AsyncInit and keel::AsyncStart are markers, not dependencies` | nothing to get |
| `stop()` or `deinit()` without `noexcept` (26) | the compiler's own: `exception specification of overriding function is more lax than base version` | add `noexcept` |

### The composition root

**`NotInApp<T>`** (27). `app.getComponent<T>()` for a `T` that is neither a component of
the App nor provided by one.

Fix: compose it. A component that derives from `T` must list `keel::Provides<T>`.

**`AmbiguousInterface<I, TypeList<Providers...>>`** (28). `app.getComponent<I>()`, and
several components provide `I`.

Fix: get a specific one by its own type, or make each of them a `keel::Alternative<I>`.
If some providers are `Provides<I>` and others `Alternative<I>`, the error is
`MixedProviders` instead (14), for the root and for `run(&I::f)` alike.

**`RunTargetNotComposed<C, F>`** (29). `run(&C::f)`, and `C` isn't composed nor provided.
An inherited member function names its base class: `&Server::serve` may be
`&Loop::serve`.

Fix: compose `C`; for an inherited member, list `keel::Provides<Base>` in the component,
or call it from a lambda.

**`RunTargetAmbiguous<I, TypeList<Providers...>>`** (30). `run(&I::f)`, and several
components provide `I`.

Fix: call it from a lambda on the one you want, or make each a `keel::Alternative<I>`.

**`RunTargetNotCallable<F>`** (31). `run(&C::f)`, and `f` can't be called on a `C&` with no
arguments (it takes parameters, or is `&&`-qualified).

Fix: wrap the call in a lambda that passes the arguments.

**`RunNotCallable<Fn>`** (32). `run(fn)`, and `fn` can be called neither with no arguments
nor with the App. Fix: take nothing, or take the App (`ServerApp&`).

**`NotAnApp<T>`** (33). `keel::run<T>(...)` with a `T` that isn't a `keel::App<...>`. Fix:
`keel::run<keel::App<...>>`, usually through an alias.

**`requireValid`** (34). A failing `static_assert(keel::requireValid<...>)` fails on the
mistake's own named assertion (`static assertion failed due to requirement
'dependencyComposed<Server, Logger>'`, in `MissingDependency<Server, Logger>`), not on the
guard's `static_assert`.

## At runtime: `keel::Error`

`keel::Error` derives from `std::logic_error`. Every message starts with `keel:`. It is
thrown for:

| Situation | Message (example) |
|---|---|
| a lookup from a constructor | `keel: getComponent<Logger>() called before the component was part of an application (from its constructor?) ...` |
| a lookup before `App::init()`, or after the caller's own `deinit()` | `keel: Server used Logger before init()` / `... after its deinit()` |
| a lookup from a disabled component | `keel: Uplink used Config, but Uplink is disabled (its enabled() returned false)` |
| a required dependency that is disabled | `keel: Dashboard uses Uplink: Uplink is disabled (its enabled() returned false)` |
| ... through an interface | `keel: Service uses Storage: its provider DiskStorage is disabled ...` |
| no enabled alternative | `keel: Repository uses Cache: none of its alternatives (RedisCache, MemoryCache) is enabled` |
| several enabled alternatives | `keel: Repository uses Cache: several of its alternatives are enabled (only one may be): RedisCache, MemoryCache` |
| an `All<I>` whose providers are all disabled | `keel: HealthEndpoint uses keel::All<HealthCheck>, and every provider of HealthCheck is disabled ...` |
| a lookup of a dependency that isn't initialized (yet, or any more) | `keel: Server used Logger before it was initialized` / `... after it was deinitialized` |
| the root asks for an alternative after an `init()` that failed before choosing | `keel: getComponent<Cache>(): init() failed before choosing among its alternatives (RedisCache, MemoryCache)` |
| the root asks for an alternative before `init()` | `keel: getComponent<Cache>(): init() chooses among its alternatives (RedisCache, MemoryCache); call it after init()` |
| `init()` twice, or `run()` after `init()` | `keel: init() can be called only once per App` |
| `start()` before `init()`, twice, after `stop()`, after a failed startup, after `deinit()` | `keel: start() called before init()`, ... |
| `setAsync()` after `init()` | `keel: setAsync() must be called before init()` |

Dependency problems found by `init()` are thrown before the dependent's `init()` runs, with
the usual [rollback](lifecycle.md#failures-during-startup). A thread for an async component
that can't be created fails startup with `std::system_error`, not `keel::Error`. Exceptions thrown by your own
components propagate unchanged, after the rollback.
