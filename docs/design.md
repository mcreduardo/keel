# Why keel

[keel](../README.md) › docs › Why keel

## The problem

Most C++ applications start as a `main()` that grows. Then someone moves everything into
an `Application` class: every subsystem becomes a member, one constructor wires them
together, and subsystems reach each other through the `Application`. That class becomes a
*god class*:

- **Hidden dependencies.** You can't tell what the exporter needs without reading its
  implementation.
- **Implicit order.** Members are constructed in declaration order and destroyed in
  reverse. Reorder two members and a worker thread logs through a logger that is already
  destroyed. Nothing in the code says the order matters.
- **Untestable parts.** To test one subsystem you build the whole application.
- **Gravity.** Every new feature is "one more member".

keel replaces the god class with three ideas:

1. **Components.** Each subsystem is its own class with a lifecycle.
2. **Dependencies as types.** A component lists what it uses in its base class. The list
   is in the header, and the compiler enforces it.
3. **A composition root.** One place (usually `main()`) lists the components of the
   application. keel checks the list at compile time and derives the order.

## What keel is not

**Not a dependency-injection framework.** The usual objections to DI containers are
hidden wiring, errors that show up at startup, unclear ownership, and a container that
spreads through the codebase. keel is built to avoid each:

| Objection | keel |
|---|---|
| Wiring is invisible (annotations, registration, container rules) | Dependencies are on the class line; the composition is one explicit list |
| Mistakes show up at startup or on first use | Missing, ambiguous and cyclic dependencies don't compile, and the error names the types; nothing may require a component that configuration can turn off, except through `Alternative<>`, whose choice is the one thing checked when the application starts |
| Ownership and lifetimes are unclear (scopes, transients) | The App owns every component, one instance each, for the life of the application |
| Interfaces and factories for everything | Meant for coarse components; plain C++ inside, interfaces only where you want to swap |
| Runtime reflection, RTTI, heap per object | No RTTI; components are stored inside the App; a dependency is looked up once, in `init()`, through a map keyed by a per-type address |

What keel adds: **lifecycle and order**. Startup and
shutdown in dependency order, a staged lifecycle (`init`/`start`/`stop`/`deinit`), rollback
of a failed startup, runtime decisions inside a checked composition, and a graph you can
inspect.

It is also **not for fine-grained objects** (components are coarse: tens per application,
not thousands), and **not for libraries**: a library shouldn't impose an application model
on its users. keel is for the program that owns `main()`.

## Other C++ libraries

Several C++ libraries do dependency injection. The main difference from keel is lifecycle
and order:

- **[Boost.DI](https://github.com/boost-ext/di)** (not part of Boost proper): compile-time
  checked, deduces constructor parameters and builds object graphs, with bindings and scopes.
  No start/stop lifecycle or derived shutdown order.
- **[fruit](https://github.com/google/fruit)**: also compile-time checked; injects
  constructors (marked with `INJECT`) from components that group bindings. No start/stop
  lifecycle.
- **[Hypodermic](https://github.com/ybainier/Hypodermic)**: a runtime container in the style of
  .NET's Autofac; registrations and resolution happen at runtime, so mistakes show up there.
- **[Kangaru](https://github.com/gracicot/kangaru)**: a container whose services are
  described by types; it builds and owns services when they are first requested. No
  start/stop lifecycle.

If what you need is constructor injection of many fine-grained objects, those are a
better fit. keel is for the coarse, long-lived parts of an application and the order they
come up and go down in.

## Core rules

These rules are the design: every feature follows from them, and keel refuses code that
breaks them, at compile time wherever possible.

1. **Every dependency is declared.** A component uses only what its `keel::Component<...>`
   list names, and that list is in its header. There is no reaching through a global, a
   singleton or an `Application` object.
   *Enforced:* `getComponent<T>()` for an undeclared `T` doesn't compile.

2. **Absence must be declared.** By default, a dependency must exist. Something that may be
   missing says so: `Optional<T>` for one component, `Optional<All<I>>` for a list that may
   be empty.
   *Enforced:* a missing dependency doesn't compile, `All<I>` with no provider doesn't
   compile, and neither does a required dependency on a component that configuration may
   turn off (a `keel::Conditional`): use `Optional<T>` for it. The exception is
   `Alternative<I>`, whose choice is checked at startup.

3. **Ambiguity is an error, never a guess.** When two components could satisfy "the" `I`,
   keel doesn't pick one for you. You say which one you mean (a
   [role interface](patterns.md#picking-one-of-several-providers-role-interfaces)), take them
   all (`All<I>`), or declare them alternatives chosen by configuration (`Alternative<I>`).
   *Enforced:* no "primary", no "first one wins"; two providers for a single dependency
   don't compile.

4. **Matching is exact.** A dependency on `T` finds the component `T`, or a component whose
   class derives publicly from `keel::Provides<T>` or `keel::Alternative<T>` (itself or
   through a base). It never finds a class just because it derives from `T`.
   *Enforced:* if that class is the only candidate, or it would be left out of `All<T>`, you
   get a compile error (`UndeclaredProvider`), not a silent miss.

5. **Order is derived, never written.** Startup and shutdown order come from the
   dependency graph. The order you list components in only breaks ties.
   *Enforced:* reordering the list can't break an order you declared (it only changes the
   order of components with no declared order between them), and a cycle doesn't compile;
   the error names the components on it.

6. **The compiler checks the structure; runtime decides only what only runtime knows.**
   Who uses whom, what exists and what's ambiguous are compile-time facts. Runtime is for
   configuration: `keel::Conditional` turns a component off, and nothing may require it
   except through `Alternative<>`; elsewhere the code declares that it copes
   (`Optional<T>`, `Optional<All<I>>`). `Alternative<>` chooses
   among implementations, and its choice (exactly one enabled) is the one thing checked
   when the application starts.

7. **One place composes the application.** The composition root lists every component.
   Components never create other components, and nothing registers itself from a static
   initializer.

8. **Fail loudly, roll back cleanly.** Errors are exceptions. A failed startup undoes
   exactly what was done, in reverse, and your exception arrives unchanged. Shutdown can't
   fail: `stop()` and `deinit()` are `noexcept`.

9. **Single-threaded unless a component says otherwise.** Concurrency is opt-in per
   component (`AsyncInit`, `AsyncStart`). Dependency order holds either way, and
   `disableAsyncForDebugging()` turns it off.

10. **No magic.** No macros to write, no RTTI, no code generation, no registration side
    effects. The component headers and the composition root are the whole story.

11. **Coarse components, plain C++ inside.** keel structures the application; it doesn't
    tell you how to write a component.
