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
- Interfaces: `keel::Provides<I>`, with ambiguity reported at compile time and access to
  interfaces from the app.
- Collections: `keel::All<I>` and `keel::Optional<keel::All<I>>`, with `getAll<I>()`.
- Decisions at runtime: `enabled()` and `keel::Alternative<I>`.
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
