# Reference

[keel](../README.md) › docs › Reference

Everything is in namespace `keel`, in `keel.hpp`. Names in `keel::detail` are internal; they
show up in compile errors, not in your code.

## API

### Declaring a component

| | |
|---|---|
| `keel::Component<Entries...>` | Base class. Each entry is one of the rows below: a dependency, and nothing else. |
| `T` | required dependency |
| `keel::Optional<T>` | optional dependency |
| `keel::After<T>` | ordering only |
| `keel::All<I>` | every provider of `I`, at least one |
| `keel::Optional<keel::All<I>>` | every provider of `I`, possibly none |

What a component is goes in its other base classes:

| | |
|---|---|
| `public keel::Provides<I>` | implements the interface `I` (derive from `I` publicly too); one per interface. Checked only for an `I` something refers to |
| `public keel::Alternative<I>` | implements `I`, one of several chosen at runtime (derive from `I` and `keel::Conditional` too). Checked like `Provides<I>`, except that it must always be a `keel::Conditional` |
| `public keel::Conditional` | configuration may turn it off (`bool enabled() const`) |
| `public keel::AsyncInit` | `init()` (and a `keel::Conditional`'s `enabled()`) on a thread of their own |
| `public keel::AsyncStart` | `start()` on a thread of its own |

The listed types may be incomplete in the component's header. Details:
[Dependencies and interfaces](dependencies.md), [Async startup](async.md).

A component that configuration may turn off also derives from `keel::Conditional`
(publicly) and overrides `bool enabled() const`. Nothing may require it: dependents list
`Optional<T>`, `After<T>` or `Optional<All<I>>`. Details:
[Decisions at runtime](runtime-decisions.md).

### Inside a component

| | |
|---|---|
| `T& getComponent<T>() const` | a required dependency (protected) |
| `T* findComponent<T>() const` | an optional dependency, or `nullptr` (protected) |
| `std::vector<I*> getAll<I>() const` | the providers of `I`, in init order, without disabled ones (protected) |
| `std::string_view componentName() const noexcept` | this component's name (public) |
| `virtual void init()`, `virtual void start()` | lifecycle hooks, may throw (default: nothing) |
| `virtual void stop() noexcept`, `virtual void deinit() noexcept` | lifecycle hooks, must not throw (default: nothing) |

On a `keel::Conditional`:

| | |
|---|---|
| `virtual bool enabled() const = 0` | `false` turns the component off; asked once, just before its `init()` (protected) |

The accessors work from `init()` on (and from `enabled()`), until the component's own
`deinit()` has run. `componentName()` is empty until the component is part of an App (in
its constructor, for instance). Details: [Lifecycle](lifecycle.md),
[Decisions at runtime](runtime-decisions.md).

### Composing

| | |
|---|---|
| `keel::compose<Cs...>(args...)` | returns a `keel::App<Cs...>`; `args` go to every component constructible from them |
| `keel::App<Cs...>` | the composed application (`Cs` may include bundles); not copyable or movable |
| `keel::Bundle<Cs...>` | a named group of components |
| `keel::requireValid<Cs...>` | `true`, or the compile error `compose<Cs...>` would give |
| `keel::isValid<Cs...>` | `true` or `false`, never a compile error |
| `keel::run<AppType>(fn, args...)` | constructs `AppType(args...)`, calls `run(fn)`, returns its result by value |

Details: [Composing an application](composition.md).

### On an `App`

| | |
|---|---|
| `void init()` | `init()` in init order (on a `keel::Conditional`, `enabled()` first); once |
| `void start()` | `start()` in init order; once, after a successful `init()` |
| `void stop() noexcept` | `stop()` in reverse order, if started |
| `void deinit() noexcept` | stops if needed, then `deinit()` in reverse order; also run by the destructor |
| `decltype(auto) run(fn)` | init, start, `fn`, stop, deinit; returns `fn`'s result. `fn`: no arguments, the App, or `&C::f` |
| `T& getComponent<T>()` | a component, or the one provider of an interface (before `init()` too, except alternatives; after it, a disabled one throws) |
| `void setAsync(bool)` | `false`: run async components on the calling thread; before `init()` |
| `static constexpr std::array<std::string_view, N> initOrder()` | component names in init order |
| `std::span<const keel::Timing> timings() const` | one entry per lifecycle call that ran |
| `std::string toDot() const` | the dependency graph in Graphviz DOT format |

Details: [Lifecycle](lifecycle.md), [Inspecting and testing](inspection-and-testing.md).

### Other

| | |
|---|---|
| `keel::Error` | composition and usage errors at runtime (derives from `std::logic_error`); see [Errors](errors.md) |
| `keel::Timing` | `{std::string_view component; keel::Phase phase; std::chrono::nanoseconds duration; bool failed;}` |
| `keel::Phase`, `keel::toString(Phase)` | `init`, `start`, `stop`, `deinit` |
| `keel::typeName<T>()` | the name keel uses for `T`, as in `initOrder()` and `Timing::component` |

## Guarantees at a glance

- A component type exists **once** per application.
- A component can reach only what it **declared**, and only from `init()` (or `enabled()`)
  on.
- What a component **requires** is always there: a missing provider, or one that
  configuration may turn off (`keel::Conditional`), doesn't compile. The one runtime
  check left is the choice among `Alternative<I>`s: exactly one enabled where an `I` is
  required, at most one where it is optional.
- **Construction, init and start** follow dependency order; **stop, deinit and
  destruction** follow the reverse.
- `start()` runs only after **every** component's `init()`.
- A failed `init()` or `start()` rolls back what was done, in reverse, and rethrows the
  exception unchanged. `stop()` and `deinit()` can't throw.
- `getAll<I>()` lists the enabled providers in init order; it is never empty for `All<I>`
  (some provider isn't conditional).
- Everything runs on the calling thread unless a component derives from `AsyncInit` or
  `AsyncStart`. Then components with no dependency path between them may run at the same
  time, and `stop()`/`deinit()` still run on the calling thread.
- No RTTI. A type's identity is the address of a per-type variable; lookups go through a
  hash map. A lookup that succeeds doesn't allocate (`getAll<I>()` allocates only the vector
  it returns); error messages are built only when it throws. Still, components resolve their
  dependencies in `init()`, so there is no lookup on the hot path.

## Requirements and platform notes

- **C++20.** Exceptions are required (keel reports runtime errors by throwing); RTTI is not.
- **Compilers.** Developed with Apple clang 17 (macOS). CI builds with GCC 11 and 14,
  Clang 15 and 21 (Linux), Apple clang and MSVC; see the README's Requirements.
- **Threads.** The CMake target `keel::keel` links `Threads::Threads`. Copying the header
  without CMake, compile and link with `-pthread` on Linux.
- **MSVC and large compositions.** The init order is computed at compile time. MSVC's
  default constexpr step limit is about ten times lower than Clang's, so compositions of
  around a hundred components or more will likely need `/constexpr:steps1000000` there.
- **Names differ by compiler.** `keel::typeName<T>()`, `initOrder()`, `Timing::component`,
  `toDot()` and error messages use the type name as the compiler spells it: anonymous
  namespaces, template arguments and spacing vary between Clang, GCC and MSVC.
- **MSVC compile errors.** keel's errors name the mistake and the component on MSVC too,
  but some types in them appear as internal aliases (`MissingDependency<Server,
  ...::check::T>` where GCC and Clang show `MissingDependency<Server, Logger>`). keel's own
  compile-fail tests check the diagnostic's name there, not every type in it.
- **CMake.** Use `add_subdirectory` or `FetchContent`, or install keel and use
  `find_package(keel CONFIG)`; either way, link `keel::keel`. The installed package accepts
  the same minor version only (before 1.0, a new minor version may change the API).
  `KEEL_INSTALL` is off when keel is a subdirectory, so a parent project that
  `install(EXPORT)`s a target linking `keel::keel` must set `KEEL_INSTALL=ON`, or CMake
  reports that keel is "not in any export set".

## Limitations

- **The set of components is fixed at compile time.** Configuration can switch components
  off (`keel::Conditional`) or choose between implementations (`Alternative<>`), and `main()` can
  choose between compositions. Components known only at runtime, such as plugins loaded
  from a configuration file, are not supported.
- **Constructor arguments go to every component that accepts them,** and a near miss (an
  ambiguous constructor, one that takes only some of the arguments, a move-only argument)
  silently falls back to the default constructor. Give components distinctive parameter
  types, such as a settings struct.
- **One start per App.** After `stop()` there is no `start()` again; create a new App.
- **Complete types for the checks.** `isValid`, `requireValid` and `compose` need every
  component defined where they first see it. A guard on a forward-declared component makes
  it "not a component" for the rest of the translation unit.
- **One error per use site.** A mistake is reported once per place that runs into it (two
  components depending on an ambiguous interface: two errors).
- **Async threads** are one per async component and phase, created when the phase begins.
  Names, stack sizes and affinity are not configurable.
- **Shared libraries.** A type's identity is the address of a per-type variable. Keep keel's
  symbols visible (default visibility) when an App spans shared libraries, or a type may
  get two identities.
- **Interfaces nothing refers to are invisible.** keel can't list a class's base classes,
  so it checks a `keel::Provides<I>` or `keel::Alternative<I>` only for an `I` that some
  entry, `getComponent<I>()` or `run(&I::f)` refers to. Otherwise it isn't checked
  (`NotDerivedFromInterface`, `InaccessibleProvides`, `InaccessibleAlternative`,
  `ProvidesAndAlternative`) and `toDot()` doesn't show it.
- `getAll<I>()` returns a new vector per call (call it in `init()`), and the composition
  root has no `getAll`.
