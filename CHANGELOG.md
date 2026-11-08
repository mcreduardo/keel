# Changelog

All notable changes to keel are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html); before 1.0, a new minor version
may change the API.

## 0.1.0 – unreleased

First release: a single C++20 header, standard library only, no RTTI.

### Added

- Components with declared dependencies: `keel::Component<Deps...>`, `getComponent<T>()`
  checked against the declared list, and `keel::compose<...>()` to build an app from a
  list of components.
- Compile-time checks of the composition, one error per mistake, naming the types
  involved: missing, duplicate, abstract or non-component entries, dependency cycles,
  invalid dependency entries, accessor calls that don't match the declaration.
- Init order derived from the dependency graph (ties keep the list order); construction
  and destruction follow it.
- Optional dependencies: `keel::Optional<T>` and `findComponent<T>()`.
- Ordering-only dependencies: `keel::InitAfter<T>`.
- Lifecycle: `init()`, `start()`, `stop()` and `deinit()` in dependency order (reverse on
  the way down); a failed `init()` or `start()` rolls back and rethrows; `stop()` and
  `deinit()` are `noexcept`. `App::run`, `keel::run` and member-function entry points.
- Interfaces: `keel::Provides<I>`, a base class a component derives from (next to
  `keel::Component<...>`, which lists dependencies and the `AsyncInit`/`AsyncStart`
  markers), with ambiguity reported at compile time and access to interfaces from the app.
  keel asks each component about the interfaces something refers to; a `Provides<I>` that
  nothing refers to is not checked. Listing it in `keel::Component<...>`
  (`ProvidesAsDependency`), deriving from it privately or twice (`InaccessibleProvides`),
  next to `keel::Alternative<I>` (`ProvidesAndAlternative`), and an `I` no dependency could
  name (`Provides<const I>`) are compile errors.
- Collections: `keel::All<I>` and `keel::Optional<keel::All<I>>`, with `getAll<I>()`.
- Decisions at runtime: `keel::Conditional`, an opt-in base class whose `enabled()` lets
  configuration turn a component off, and `keel::Alternative<I>`, a base class like
  `keel::Provides<I>` (each component also a `keel::Conditional`), to choose one of several
  implementations. Like `Provides<I>`, it is checked only for an I something refers to.
  Listing it in `keel::Component<...>` (`AlternativeAsDependency`), deriving from it
  privately or twice (`InaccessibleAlternative`), and `Alternative<const I>` (an error where
  the component is constructed) are compile errors; an alternative that isn't a
  `keel::Conditional` is `AlternativeNotConditional<C>`, checked whether or not something
  refers to its interface. Nothing may require a
  conditional component except through `Alternative<I>`: `RequiredConditional`,
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
  `start()` on a thread of its own, scheduled by dependencies; `App::setAsync(false)` runs
  everything on the calling thread.
- Successful lookups don't allocate.
- CMake: the `keel::keel` target (C++20, links `Threads::Threads`), install rules and a
  package config for `find_package(keel CONFIG)`.
- Tests (including compile-fail tests for each diagnostic), examples run as tests,
  documentation in `docs/`, and a GitHub Actions workflow.
