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

- **`Optional<T>` / `InitAfter<T>` on a base class never match.** `keel::Optional<IMetrics>`
  with only `PrometheusMetrics : IMetrics` composed compiles and returns nullptr, and
  `keel::InitAfter<IMetrics>` silently orders nothing, because components are matched by
  exact type. The same goes for a T that is a class but not a component
  (`InitAfter<std::string>`). Revisit with interfaces (`Provides<I>`); a check for "a
  composed component derives from T" would need T to be complete.

- **`start()` runs once per App.** After `stop()` the app can only be deinitialized;
  `start()` again throws `keel::Error`. Alternative: allow stop/start cycles (restart
  without re-init), as `framework/` does. Not done because every component would have to
  support being started again, and nothing needs it yet.

- **A failed `start()` deinitializes everything.** Like a failed `init()`, the app is then
  done: started components are stopped in reverse, every component is deinitialized, the
  exception propagates. Alternative (the `framework/` implementation, whose rollback only
  stops): leave the app initialized for the caller to deinit or retry. Chosen for one rule:
  a failed startup leaves nothing running or held.

- **`run(&C::f)` matches C exactly.** A member function inherited from a non-component base
  class has the base's type (`&Server::serve` is `void (Base::*)()`), so it is reported as
  not composed. Revisit with interfaces (`Provides<I>`): a base implemented by exactly one
  component could resolve to it.

- **A generic lambda passed to `run(fn)` is called with the App without checking first.**
  So an error in its body is reported once, by the compiler, instead of a second
  "not callable" error from keel. Trade-off: a generic lambda that can't take the App at
  all (`[](auto x, auto y) {}`) gets the compiler's "no matching function" rather than
  keel's `RunNotCallable`. Non-generic callables are still checked by keel.
