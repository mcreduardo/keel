# Changelog

All notable changes to keel are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html); before 1.0, a new minor version
may change the API.

## Unreleased

### Changed

- Merged diagnostics (what compiles is unchanged; one error per mistake as before):
  - `InvalidDependency<C, Entry>` now also covers what were `ProvidesAsDependency`,
    `AlternativeAsDependency`, `AsyncAsDependency`, `MisplacedAsync`, `BundleAsDependency`,
    `MisnestedAll`, and listing `keel::Provides<I>`, `keel::Alternative<I>`,
    `keel::AsyncInit` or `keel::AsyncStart` twice (one error per type an entry is about).
  - `InaccessibleProvides<C, I>`, `InaccessibleAlternative<C, I>`, `InaccessibleConditional<C>`
    and `InaccessibleAsync<C>` → `InaccessibleBase<C, Base>` (Base is `keel::Provides<I>`,
    `keel::Alternative<I>`, `keel::Conditional`, `keel::AsyncInit` or `keel::AsyncStart`).
  - `UndeclaredSubclass<C, T, Derived>` and `LeftOutOfAll<C, T, Derived>` →
    `UndeclaredProvider<C, T, Derived>`.
  - `RequiredConditional<C, T>` and `RequiredConditionalProvider<C, I, Provider>` →
    `RequiredConditional<C, T, Provider>` (Provider is T itself for a conditional component).
  - `examples/compile_errors.cpp` has 35 cases instead of 48, renumbered.
- Clang's first line of a compile-time diagnostic names the diagnostic with its types
  (`requirement 'fixed<keel::detail::MissingDependency<Server, Logger>>'`) instead of a
  per-diagnostic variable (`'dependencyComposed<Server, Logger>'`). Messages are unchanged.

### Fixed

- A root `getComponent<I>()` of an alternative, or `start()`, called while `init()` is still
  running (from a component's hook) no longer reports a failed `init()`: the messages are
  `init() hasn't chosen among its alternatives yet (...)` and `start() called before init() returned`.
  `start()` called while `start()` runs reports `start() called while start() runs` instead
  of a failed `init()` or `start()`.

## 0.1.0 – 2026-11-09

First release: a single C++20 header, standard library only, no RTTI.

### Added

- Components with declared dependencies: `keel::Component<Deps...>`, `getComponent<T>()`
  checked against the declared list, and `keel::compose<...>()` to build an app from a
  list of components. The `keel::Component<...>` list holds only dependencies; what a
  component is (`keel::Provides<I>`, `keel::Alternative<I>`, `keel::Conditional`,
  `keel::AsyncInit`, `keel::AsyncStart`) goes in its other base classes. Listing one of
  them, or deriving from it privately or twice, is a compile error.
- Compile-time checks of the composition, one error per mistake, naming the types
  involved: missing, duplicate, abstract or non-component entries, dependency cycles,
  invalid dependency entries, accessor calls that don't match the declaration.
- Init order derived from the dependency graph (ties keep the list order); construction
  and destruction follow it.
- Optional dependencies: `keel::Optional<T>` and `findComponent<T>()`.
- Ordering-only dependencies: `keel::After<T>`.
- Lifecycle: `init()`, `start()`, `stop()` and `deinit()` in dependency order (reverse on
  the way down); a failed `init()` or `start()` rolls back and rethrows; `stop()` and
  `deinit()` are `noexcept`. `App::run`, `keel::run` and member-function entry points.
- Interfaces: `keel::Provides<I>`, with ambiguity reported at compile time and access to
  interfaces from the app. keel asks each component about the interfaces something refers
  to; a `Provides<I>` that nothing refers to is not checked. `ProvidesAsDependency`,
  `InaccessibleProvides`, `ProvidesAndAlternative`, and `Provides<const I>` (an error where
  the component is constructed) are compile errors.
- Collections: `keel::All<I>` and `keel::Optional<keel::All<I>>`, with `getAll<I>()`.
- Decisions at runtime: `keel::Conditional`, an opt-in base class whose `enabled()` lets
  configuration turn a component off (`InaccessibleConditional` if it isn't a public base,
  once), and `keel::Alternative<I>` (each component also a `keel::Conditional`) to choose
  one of several implementations. Like `Provides<I>`, an alternative is checked only for
  an I something refers to (`AlternativeAsDependency`, `InaccessibleAlternative`,
  `Alternative<const I>`), except that it must be a `keel::Conditional`
  (`AlternativeNotConditional<C>`, always checked). Nothing may require a conditional
  component except through `Alternative<I>`: `RequiredConditional`,
  `RequiredConditionalProvider`, `OnlyConditionalProviders` and `RunTargetConditional` are
  compile errors. So the only composition failures left to runtime are an
  `Alternative<I>` with none or several enabled.
  Code written against earlier development snapshots, where every component had a virtual
  `enabled()`: derive the components that override it from `keel::Conditional`. An old
  `bool enabled() const` without `override` and without `keel::Conditional` now compiles
  and is never asked; writing `override` catches it.
- Groups of components: `keel::Bundle<...>`; composition guards `keel::isValid` and
  `keel::requireValid`.
- Introspection: `initOrder()`, `timings()` and `toDot()` (Graphviz).
- Async startup: `keel::AsyncInit` and `keel::AsyncStart` run a component's `init()` or
  `start()` on a thread of its own, scheduled by dependencies (`AsyncAsDependency`,
  `InaccessibleAsync`); `App::setAsync(false)` runs everything on the calling thread.
- Successful lookups don't allocate.
- CMake: the `keel::keel` target (C++20, links `Threads::Threads`), install rules and a
  package config for `find_package(keel CONFIG)`.
- `keel.hpp` is generated from development headers in `include/keel/` by
  `tools/amalgamate.py`; CI checks that it is up to date. `#include <keel.hpp>` is unchanged.
- Tests (including compile-fail tests for each diagnostic), examples run as tests,
  documentation in `docs/`, and a GitHub Actions workflow.
