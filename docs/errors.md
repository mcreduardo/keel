# Errors

[keel](../README.md) › docs › Errors

## At compile time

Each mistake produces **one** error. The diagnostic is a class template named after the
mistake, and its template arguments name the types involved. With Clang, the first line
already says it:

```
error: static assertion failed due to requirement 'fixed<keel::detail::MissingDependency<Server, Logger>>':
keel: missing dependency: the Component in MissingDependency<Component, Dependency> needs Dependency, and
no composed component is or provides it.
```

and the notes below it end with the line of your code that composed the App. Read the message as a sentence with the
template parameters filled in: `Server` needs `Logger`.

"One error" is per mistake *and* per place that runs into it: two components that both
depend on an ambiguous interface give two errors, like an undeclared variable used twice.
A mistake in a component's own list (a duplicate, an invalid entry) suppresses the errors
its accessors would add for that type, so the list is the one error.

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
(`class T : public keel::Component<...>, public keel::Provides<T>`) so the subclass is
found as `T`. For a `keel::Conditional`, check the signature of `enabled()` (34): it must
be `bool enabled() const`.

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
of entry orders (required, `Optional`, `After`, `All`), so each can close a cycle.

A component that provides `I` and lists `I` in any form (`I`, `Optional<I>`, `After<I>`,
`All<I>`, `Optional<All<I>>`, `After<All<I>>`) depends on itself:
`DependencyCycle<TypeList<C>>`, whatever the other providers of `I` (with a second
provider it is a cycle, not `AmbiguousDependency`). Its subclasses inherit both. So does
`After<C>` in `C` itself. An aggregator of `I` provides
another interface instead (see
[Collections](dependencies.md#collections-keelalli-and-keeloptionalkeelalli)):

```cpp
class AllChecks : public keel::Component<keel::All<HealthCheck>>, public keel::Provides<HealthCheck>,
                  public HealthCheck {};
keel::compose<AllChecks, Database>();  // DependencyCycle<TypeList<AllChecks>>

class HealthSummary : public keel::Component<keel::All<HealthCheck>>, public keel::Provides<HealthReport>,
                      public HealthReport {};  // fine
```

**`InvalidDependency<C, Entry>`** (8). An entry of a component's list isn't a dependency:

- a keel marker that is a base class: `keel::Provides<I>`, `keel::Alternative<I>`,
  `keel::AsyncInit`, `keel::AsyncStart`. Fix: inherit it next to `keel::Component<...>`:
  `class S3Storage : public keel::Component<>, public keel::Provides<Storage>, public Storage`.
  Dependents of `I` get no second error.
- a `keel::Bundle<...>`. Fix: list the components it uses, each on its own; a bundle only
  groups components for `compose<...>`.
- `keel::All<keel::Optional<I>>`. Fix: write `keel::Optional<keel::All<I>>` for a list that
  may be empty.
- a type no dependency can name: not a class, `const` or `volatile`, a reference, or itself
  a marker (`keel::Optional<keel::AsyncInit>`, `keel::After<keel::Optional<I>>`, ...). Fix: name
  the plain class, e.g. `keel::Optional<Metrics>`. A dependency is accessed as `T&` or `T*`
  already; constness is up to the code that uses it.

```cpp
class Server : public keel::Component<keel::Optional<const Metrics>> {};  // InvalidDependency<Server, keel::Optional<const Metrics>>
```

Several invalid entries about one type (`keel::After<Core>` and `Core`, or the same entry
twice) are one error, at the first. A lookup of that type adds no error; lookups of other
types are checked as usual.

**A dependency listed twice** (9). `T`, `Optional<T>`, `After<T>`, `All<T>`,
`Optional<All<T>>` and `After<All<T>>` all name `T`; two of them in one list is an error.

```cpp
class Server : public keel::Component<Logger, keel::Optional<Logger>> {};
// keel: a dependency is listed twice in keel::Component<...> (T, keel::Optional<T>, ...)
```

Fix: keep the one entry that says what you mean.

### Interfaces and collections

**`NotDerivedFromInterface<C, I>`** (10). `C` derives from `keel::Provides<I>` (or
`keel::Alternative<I>`), but doesn't derive from `I` publicly and unambiguously, or `I`
isn't defined at that point.

```cpp
class Archive : public keel::Component<>, public keel::Provides<Storage> {};  // forgot ", public Storage"
class Service : public keel::Component<Storage> {};
keel::compose<Service, Archive>();  // NotDerivedFromInterface<Archive, Storage>
```

Fix: derive publicly from `I`.

keel can't list a class's base classes (C++20 has no reflection), so it checks a
`keel::Provides<I>` or `keel::Alternative<I>` only when something refers to `I`: a
dependency of a composed component, or the root's `getComponent<I>()` or `run(&I::f)`. A
provider of an interface nothing uses isn't checked. The error is reported once per
mistaken provider, at the first dependency on `I` (in `compose<...>` order), and the other
dependencies on `I` aren't reported as missing or ambiguous: that follows from the mistake.
`InaccessibleBase` (11) on a `keel::Provides<I>` or `keel::Alternative<I>` base and
`ProvidesAndAlternative` (16) work the same way.

**`InaccessibleBase<C, Base>`** (11). `C` derives from one of keel's base classes
(`keel::Provides<I>`, `keel::Alternative<I>`, `keel::Conditional`, `keel::AsyncInit`,
`keel::AsyncStart`) privately (the default for a `class`), protectedly, or more than once
(itself and through a base that has it already), so keel can't see it.

```cpp
class S3Storage : public keel::Component<>, keel::Provides<Storage>, public Storage {};
class Service : public keel::Component<Storage> {};
keel::compose<Service, S3Storage>();  // InaccessibleBase<S3Storage, keel::Provides<Storage>>
```

Fix: derive from it publicly, once; if a base class already derives from it, don't derive
from it again (a subclass inherits it). `keel::Conditional`, `keel::AsyncInit` and
`keel::AsyncStart` are checked for every composed component; `Provides<I>` and
`Alternative<I>` only for an `I` something refers to.

**`AmbiguousDependency<C, I, TypeList<Providers...>>`** (12). `C` needs one `I` (it lists
`I`, `Optional<I>` or `After<I>`), and several composed components are or provide it. (If
`C` is one of them, it is a `DependencyCycle` instead.)

```cpp
class Service : public keel::Component<Storage> {};
keel::compose<Service, DiskStorage, MemoryStorage>();
// AmbiguousDependency<Service, Storage, TypeList<DiskStorage, MemoryStorage>>
```

Fix, depending on what you mean: compose only one; depend on the one you want by its
own type (or a [role interface](patterns.md#picking-one-of-several-providers-role-interfaces));
use every provider with `keel::All<I>` (come after every provider with
`keel::After<keel::All<I>>`); or let configuration choose: make each one a
`keel::Alternative<I>` and a `keel::Conditional`, and enable one at runtime (or create the
configured one in a single component: see
[Decisions at runtime](runtime-decisions.md#choose-an-implementation)).

**`MixedProviders<I, TypeList<Provides...>, TypeList<Alternatives...>>`** (13). Something
needs one `I`, and some of its providers derive from `Provides<I>` while others derive from
`Alternative<I>`. Reported once per `I`, however many places need it.

Fix: make every provider an `Alternative<I>` and a `keel::Conditional` (a do-nothing
default is an alternative too), or compose only one.

**`UndeclaredProvider<C, T, Derived>`** (14). `C` depends on `T` (required, `Optional`,
`After`, `All`, `Optional<All>` or `After<All>`), and `Derived` derives from `T` without
providing it.
keel matches exact types, so `Derived` would never be found: a missing dependency, or a
component silently left out of `All<T>`.

```cpp
class PrometheusMetrics : public keel::Component<>, public Metrics {};
class Server : public keel::Component<Metrics> {};
keel::compose<Server, PrometheusMetrics>();  // UndeclaredProvider<Server, Metrics, PrometheusMetrics>

class Clock : public keel::Component<> {};
class FakeClock : public Clock {};
class Service : public keel::Component<Clock> {};
keel::compose<Service, FakeClock>();  // UndeclaredProvider<Service, Clock, FakeClock>
```

Fix: derive `Derived` from `keel::Provides<T>` too (`public keel::Provides<T>`); if `T` is
itself a component, compose `T`, or derive `T` from `keel::Provides<T>`
(`class Clock : public keel::Component<>, public keel::Provides<Clock>`) so that its
subclasses inherit it; or depend on `Derived`.

**`MissingProvider<C, I>`** (15). `C` lists `keel::All<I>`, which needs at least one
provider, and no composed component is or provides `I`.

Fix: compose a provider, or depend on `keel::Optional<keel::All<I>>` if none is fine.

**`ProvidesAndAlternative<C, I>`** (16). `C` derives from both `keel::Provides<I>` and
`keel::Alternative<I>`. Checked like `NotDerivedFromInterface`: only for an `I` something
refers to.

```cpp
class S3Storage : public keel::Component<>, public keel::Alternative<Storage>,
                  public keel::Provides<Storage>, public Storage, public keel::Conditional { ... };
class Service : public keel::Component<Storage> {};
keel::compose<Service, S3Storage>();  // ProvidesAndAlternative<S3Storage, Storage>
```

Fix: keep `Alternative<I>` if configuration chooses it among other implementations,
`Provides<I>` if it is always the one.

**`keel::Provides<I>` or `keel::Alternative<I>` with an `I` no dependency could name**
(`keel::Provides<const Storage>`, `keel::Alternative<int>`, a reference or a keel marker).
It is rejected where the component is constructed, with the message `keel::Provides<I>
needs an interface I that a dependency could name` (or `keel::Alternative<I> ...`). `I` may
be incomplete (forward-declared). `keel::isValid` doesn't see it. Fix: name the plain
class: `keel::Provides<Storage>`.

### Inside a component

These fire where the component's code is compiled, composed or not. The message says which
accessor to use instead.

| Mistake (number) | Message (shortened) | Fix |
|---|---|---|
| `getComponent<T>()` for a `T` the list doesn't name (17) | `getComponent<T>() requires T to be listed in this component's keel::Component<...>` | list `T` |
| `getComponent<T>()` on `Optional<T>` (18) | `T is declared keel::Optional<T>; use findComponent<T>(), which may return nullptr` | `findComponent<T>()` |
| `getComponent<T>()` on `After<T>` (19) | `T is declared keel::After<T>, which is ordering only; replace it with T to use T` | list `T` instead |
| `findComponent<T>()` on a required `T` (20) | `T is a required dependency; use getComponent<T>(), which never returns nullptr` | `getComponent<T>()` |
| `findComponent<T>()` without `Optional<T>` | `findComponent<T>() requires keel::Optional<T> in this component's keel::Component<...>` | list `Optional<T>` |
| `getAll<I>()` on a required `I` (21), `Optional<I>` or `After<I>` | `I is a required dependency (one provider); use getComponent<I>(), or replace it with keel::All<I> ...` | `getComponent`/`findComponent`, or list `All<I>` |
| `getAll<I>()` without `All<I>` | `getAll<I>() requires keel::All<I> or keel::Optional<keel::All<I>> ...` | list one of them |
| `getComponent<T>()`/`findComponent<T>()` on `All<T>` | `T is declared keel::All<T> or keel::Optional<keel::All<T>>; use getAll<T>()` | `getAll<T>()` |
| any accessor on `After<All<T>>` | `T is declared keel::After<keel::All<T>>, which is ordering only; replace it with keel::All<T> ...` | list `All<T>` (or `Optional<All<T>>`) and use `getAll<T>()` |
| `getComponent<keel::AsyncInit>()` (also `Provides<I>`, `Alternative<I>`, `AsyncStart`; any accessor) | `keel::Provides<I>, keel::Alternative<I>, keel::AsyncInit and keel::AsyncStart are base classes, not dependencies` | nothing to look up |
| `stop()` or `deinit()` without `noexcept` (22) | the compiler's own: `exception specification of overriding function is more lax than base version` | add `noexcept` |

### The composition root

**`NotInApp<T>`** (23). `app.getComponent<T>()` for a `T` that is neither a component of
the App nor provided by one.

Fix: compose it. A component that derives from `T` must also derive from
`keel::Provides<T>`. If a provider derives from `keel::Provides<T>` or
`keel::Alternative<T>` but can't be used as a `T`, and no component depends on `T`, the
error is the provider's own (10, 11 or 16) instead, here and for `run(&T::f)`.

**`AmbiguousInterface<I, TypeList<Providers...>>`** (24). `app.getComponent<I>()`, and
several components provide `I`.

Fix: get a specific one by its own type, or make each of them a `keel::Alternative<I>` and
a `keel::Conditional`.
If some providers derive from `Provides<I>` and others from `Alternative<I>`, the error is
`MixedProviders` instead (13), for the root and for `run(&I::f)` alike.

**`RunTargetNotComposed<C, F>`** (25). `run(&C::f)`, and `C` isn't composed nor provided.
An inherited member function names its base class: `&Server::serve` may be
`&Loop::serve`.

Fix: compose `C`; for an inherited member, derive the component from
`keel::Provides<Base>` too, or call it from a lambda.

**`RunTargetAmbiguous<I, TypeList<Providers...>>`** (26). `run(&I::f)`, and several
components provide `I`.

Fix: call it from a lambda on the one you want, or make each a `keel::Alternative<I>` and a
`keel::Conditional`.

**`RunTargetNotCallable<F>`** (27). `run(&C::f)`, and `f` can't be called on a `C&` with no
arguments (it takes parameters, or is `&&`-qualified).

Fix: wrap the call in a lambda that passes the arguments.

**`RunNotCallable<Fn>`** (28). `run(fn)`, and `fn` can be called neither with no arguments
nor with the App. Fix: take nothing, or take the App (`ServerApp&`).

**`NotAnApp<T>`** (29). `keel::run<T>(...)` with a `T` that isn't a `keel::App<...>`. Fix:
`keel::run<keel::App<...>>`, usually through an alias.

**`requireValid`** (30). A failing `static_assert(keel::requireValid<...>)` fails on the
mistake's own named assertion (`static assertion failed due to requirement
'fixed<keel::detail::MissingDependency<Server, Logger>>'`), not on the guard's `static_assert`.

### Conditional components

A [`keel::Conditional`](runtime-decisions.md#keelconditional-a-component-that-may-be-off)
component may be turned off by configuration, so nothing may require it, except through
`keel::Alternative<I>`, whose choice is checked at startup.

**`RequiredConditional<C, T, Provider>`** (31). `C` lists `T` as a required dependency, and
`Provider`, which is `T` itself or its one provider (deriving from `keel::Provides<T>`), is
a `keel::Conditional`. Also when `C` is conditional itself: each may be off on its own.

```cpp
class Uplink : public keel::Component<Config>, public keel::Conditional { ... };
class Dashboard : public keel::Component<Uplink> {};  // RequiredConditional<Dashboard, Uplink, Uplink>
```

Fix: list `keel::Optional<T>` and use `findComponent<T>()`, which returns `nullptr` while
it is disabled (or `keel::After<T>` for ordering only). Or, if `T` is always needed, don't
make it conditional; for an interface, compose a provider that is always there, e.g. a
component that creates the configured implementation, possibly a do-nothing one (see
[Decisions at runtime](runtime-decisions.md#turning-a-feature-off-which-tool-to-use)), or
make every implementation a `keel::Alternative<T>` (each a `keel::Conditional` already),
whose choice is checked at startup.

**`OnlyConditionalProviders<C, I, TypeList<Providers...>>`** (32). `C` lists
`keel::All<I>`, which is never empty, and every provider of `I` is a `keel::Conditional`
(alternatives included), so they could all be off.

Fix: depend on `keel::Optional<keel::All<I>>` if none is fine, or compose a provider that
isn't conditional.

**`AlternativeNotConditional<C>`** (33). `C` derives from a `keel::Alternative<I>` but not
from `keel::Conditional`, so configuration can't turn it off. A member called `enabled()`
doesn't count: keel only asks a `keel::Conditional`. Reported once per component, whatever
its interfaces, and whether or not something refers to them.

Fix: derive publicly from `keel::Conditional` and override `bool enabled() const`; or
derive from `keel::Provides<I>` instead if it is always the one.

**`enabled()` with the wrong signature** (34). `bool enabled()` without `const` doesn't
override `keel::Conditional`'s, so the component stays abstract: `AbstractComponent<T>` (3),
whose message reminds you of `bool enabled() const`. With `override`, the compiler reports
its own error first ("marked 'override' but does not override"), followed by
`AbstractComponent<T>`.

**`RunTargetConditional<C, Provider>`** (35). `run(&C::f)`, and the one component that is
or provides `C` is a `keel::Conditional`, so there may be nothing to call `f` on.
(`run(&I::f)` on `keel::Alternative<I>` providers is allowed: the choice is checked at
startup.)

```cpp
class Uplink : public keel::Component<>, public keel::Conditional { public: int send(); ... };
app.run(&Uplink::send);  // RunTargetConditional<Uplink, Uplink>
```

Fix: call it from a lambda that decides what to do when it is off, or run a member function
of a component that is always there.

## At runtime: `keel::Error`

`keel::Error` derives from `std::logic_error`. Every message starts with `keel:`. It is
thrown for:

| Situation | Message (example) |
|---|---|
| a lookup from a constructor | `keel: getComponent<Logger>() called before the component was part of an application (from its constructor?) ...` |
| a lookup before `App::init()`, or after the caller's own `deinit()` | `keel: Server used Logger before init()` / `... after its deinit()` |
| a lookup from a disabled component | `keel: Uplink used Config, but Uplink is disabled (its enabled() returned false)` |
| the root asks for a disabled component after `init()` (also `run(&Uplink::f)`) | `keel: getComponent<Uplink>(): Uplink is disabled (its enabled() returned false)` |
| ... through an interface | `keel: getComponent<Storage>(): its provider DiskStorage is disabled ...` |
| no enabled alternative | `keel: Repository uses Cache: none of its alternatives (RedisCache, MemoryCache) is enabled` |
| several enabled alternatives | `keel: Repository uses Cache: several of its alternatives are enabled (only one may be): RedisCache, MemoryCache` |
| a lookup of a dependency that isn't initialized (yet, or any more) | `keel: Server used Logger before it was initialized` / `... after it was deinitialized` |
| the root asks for an alternative after an `init()` that failed before choosing | `keel: getComponent<Cache>(): init() failed before choosing among its alternatives (RedisCache, MemoryCache)` |
| the root asks for an alternative before `init()` | `keel: getComponent<Cache>(): init() chooses among its alternatives (RedisCache, MemoryCache); call it after init()` |
| the root asks for an alternative while `init()` runs, before it has chosen | `keel: getComponent<Cache>(): init() hasn't chosen among its alternatives yet (RedisCache, MemoryCache)` |
| `init()` twice, or `run()` after `init()` | `keel: init() can be called only once per App` |
| `start()` before `init()` (or while it runs), while `start()` runs, twice, after `stop()`, after a failed startup, after `deinit()` | `keel: start() called before init()`, ... |
| `disableAsyncForDebugging()` after `init()` | `keel: disableAsyncForDebugging() must be called before init()` |

The alternatives' failures (none or several enabled) are the only dependency problems
left to `init()`. They are thrown before the dependent's `init()` runs, with
the usual [rollback](lifecycle.md#failures-during-startup). A thread for an async component
that can't be created fails startup with `std::system_error`, not `keel::Error`. Exceptions thrown by your own
components propagate unchanged, after the rollback.
