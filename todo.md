# To do

- **Constructor arguments that almost match.** `compose<...>(args...)` gives each component
  the arguments if it can be constructed from them, and default-constructs it otherwise. A
  near-miss falls back to the default constructor silently:
  - the constructor call is ambiguous;
  - the constructor takes only some of the arguments;
  - an argument is move-only (arguments are passed as lvalues);
  - an implicit conversion picks a different constructor than intended.

  Decide whether a near-miss should be a compile error, or keep the rule and document it.

- **Warn about unused dependencies.** A component declares a dependency but never calls
  `getComponent<T>()` for it. The dependency still constrains the order and must be composed,
  so it should be reported. The compiler can't see which `getComponent<T>()` calls a
  component makes, so this would be a runtime check: record the calls and report the
  dependencies that were never fetched, for example after `init()` or at shutdown.

- **Compose with an explicit init order.** Some users will want to state the init (and
  construction) order themselves instead of having it derived from the dependencies. A
  variant of `compose` would take the components in the order the user wants and check it
  at compile time: every component comes after its dependencies, nothing is missing or
  listed twice. A wrong order fails compilation with an error that names the components
  involved; keel never reorders the list.

- **A component that derives from T without `Provides<T>` is a compile error.** If a
  component depends on T (required, `Optional<T>` or `InitAfter<T>`), nothing composed is or
  provides T, and a composed component derives from T, keel reports
  `UndeclaredProvider<Component, T, Derived>` instead of a missing dependency (required) or
  a silent nullptr / no ordering (Optional, InitAfter). Alternatives: resolve T to the
  derived component automatically; or keep ignoring it. Chosen because dependencies match
  by exact type or declared `Provides<T>`, one rule, and the silent cases hid the mistake.
  Cost: a composition that derives from T on purpose without providing it must add
  `Provides<T>` or not depend on T. When T is itself a component (`FakeClock : Clock`), the
  subclass can't list `Provides<Clock>` (a second `Component<>` base), so the error is
  `UndeclaredSubclass<C, T, Derived>`: compose T, or let T provide itself
  (`Clock : Component<Provides<Clock>>`), which its subclasses inherit. Still silent: an
  `InitAfter<T>`/`Optional<T>` whose T nothing composed derives from
  (`InitAfter<std::string>`, or a forgotten compose), and an I that a composed class derives
  from twice (non-virtual diamond: not convertible, so no UndeclaredProvider, and
  `Optional<I>` stays nullptr).
  For `All<I>` and `Optional<All<I>>` a similar check runs even when I has providers:
  `LeftOutOfAll<C, I, Derived>`, because a composed class that derives from I without
  providing it would silently be left out of the list. (For a single dependency with one
  provider it doesn't: the dependency resolves, and the other class may be unrelated.) There
  is no opt-out: a component that derives from I for unrelated reasons can't stay out of
  `All<I>` except by not deriving from I. The collecting component itself is exempt: one
  that lists `All<I>` and derives from I without providing it is accepted (and not in its
  own list).

- **`InitAfter<I>` with several providers orders after all of them.** A required or
  `Optional<I>` dependency with two providers is an ambiguity error
  (`AmbiguousDependency`), but `InitAfter<I>` is ordering only and gives no access, so "after
  every provider" is well defined. Alternative: report it as ambiguous too, for one rule.

- **Providing and depending on the same interface.** `Component<Provides<I>, I>` is reported
  as `DependencyCycle<TypeList<C>>` (one provider) or `AmbiguousDependency` (more), not with
  its own message. Alternative: a dedicated check ("C provides I and depends on I"). Not done:
  the existing errors name the type, and the case looks rare. `Component<Provides<I>,
  InitAfter<I>>` is fine: it orders after the other providers of I (a decorator or chain).

- **`App::getComponent<I>()` is resolved at compile time**, so it works before `init()`.
  This will conflict with `Alternative<I>` (milestone 8), where the provider is chosen at
  runtime. Revisit then.

- **The ambiguity message doesn't mention `Alternative<I>` yet.** It suggests `All<I>`;
  add the `Alternative<I>` hint in milestone 8.

- **`All<I>` leaves out the component itself.** A component that provides I and lists
  `All<I>` (a composite: a HealthCheck that checks all the others) gets every other
  provider, and is ordered after them; "at least one" counts the others. Same rule as
  `InitAfter<I>`. Alternative: report it as a cycle, like `Component<Provides<I>, I>`.
  Not obvious consequences, reported only as `DependencyCycle<...>`: two composites of the
  same I each come after the other (compile-fail test `two_composites`); and a subclass of
  a composite (`Y : X` with `X : Component<Provides<I>, All<I>>`) inherits both entries, so
  X and Y each come after the other. A dedicated message could explain these.

- **One entry per type, collections included.** T, `Optional<T>`, `InitAfter<T>`, `All<T>`
  and `Optional<All<T>>` all name T, so listing two of them is the duplicate error. Each
  entry decides the one accessor (getComponent, findComponent, getAll). Alternative: allow
  combinations such as `T, All<T>`; none adds anything the single entries can't express.

- **`getAll<I>()` returns a new `std::vector<I*>` per call.** Simple, and the documented
  use is to call it once in `init()` and keep it. Alternative: a `std::span` over a list
  the registry keeps per interface (no allocation, but the pointers would have to be stored
  per interface type, not as `void*`).

- **`All<Optional<I>>` is `MisnestedAll<C, I>`.** The accessors skip their checks for that
  I, so a getComponent/findComponent/getAll on it adds no second error. Other nestings
  (`All<const I>`, `All<InitAfter<I>>`, `Optional<All<Optional<I>>>`) are
  `InvalidDependency<C, Entry>`.

- **No `App::getAll<I>()` at the composition root.** The root gets one component by type;
  nothing needs the list there yet. Add it if an example does.

- **`start()` runs once per App.** After `stop()` the app can only be deinitialized;
  `start()` again throws `keel::Error`. Alternative: allow stop/start cycles (restart
  without re-init), as `framework/` does. Not done because every component would have to
  support being started again, and nothing needs it yet.

- **A failed `start()` deinitializes everything.** Like a failed `init()`, the app is then
  done: started components are stopped in reverse, every component is deinitialized, the
  exception propagates. Alternative (the `framework/` implementation, whose rollback only
  stops): leave the app initialized for the caller to deinit or retry. Chosen for one rule:
  a failed startup leaves nothing running or held.

- **`run(&C::f)` resolves C like `App::getComponent<C>()`.** C is a composed component or
  an interface with exactly one provider (`run(&Shell::serve)`). A member function inherited
  from a base class has the base's type (`&Server::serve` is `void (Loop::*)()`), so it works
  if the component `Provides<Loop>`, and is otherwise `RunTargetNotComposed<Loop, F>`, whose
  message suggests `Provides<Loop>` or a lambda. Alternative: call it on the one composed
  component that derives from Loop. Not done for the same reason as `UndeclaredProvider`:
  one matching rule everywhere.

- **A generic lambda passed to `run(fn)` is called with the App without checking first.**
  So an error in its body is reported once, by the compiler, instead of a second
  "not callable" error from keel. Trade-off: a generic lambda that can't take the App at
  all (`[](auto x, auto y) {}`) gets the compiler's "no matching function" rather than
  keel's `RunNotCallable`. Non-generic callables are still checked by keel.

- **The cycle error is slow for large apps.** Naming the components on a cycle uses an O(N³)
  reachability pass, which exceeds clang's constexpr step limit at about 150 components
  (MSVC's limit is lower), so a large app with a cycle gets a step-limit error instead of
  `DependencyCycle<...>`. The normal path is O(N²). A strongly-connected-components pass
  (Tarjan, O(N²) on the matrix) would fix it.

- **Large apps on MSVC may need `/constexpr:steps1000000`.** The order is computed at
  compile time; the 150-component test needs about 511k clang constexpr steps (nearly all in
  `topologicalOrder`), above MSVC's default of about 100k. The tests set the flag; the
  docs must mention it (milestone 12). Alternative: a cheaper sort.
