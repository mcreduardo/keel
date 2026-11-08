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
  error is `UndeclaredSubclass<C, T, Derived>`: compose T, or let T provide itself
  (`Clock : Component<>, Provides<Clock>`), which its subclasses inherit. Still silent: an
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

- **Providing and depending on the same interface.** `C : Component<I>, Provides<I>` is
  reported as `DependencyCycle<TypeList<C>>` (one provider) or `AmbiguousDependency` (more),
  not with its own message. Alternative: a dedicated check ("C provides I and depends on
  I"). Not done: the existing errors name the type, and the case looks rare.
  `C : Component<InitAfter<I>>, Provides<I>` is fine: it orders after the other providers of I (a decorator or chain).
  The same holds for `Alternative<I>`: an alternative that depends on I is ordered after
  every alternative, itself included, so it is `DependencyCycle<TypeList<C>>`. And an
  alternative that needs a dependent of I is on a cycle with it (compile-fail test
  `alternative_cycle`), even though only one alternative will be enabled.

- **`App::getComponent<T>()`: compile time for one provider, runtime for alternatives.** A
  component, or an interface with one `Provides<>` provider, is resolved at compile time and
  usable before `init()` (to configure it); after `init()`, a disabled one (a
  `keel::Conditional`) throws `keel::Error`. Unlike a component's dependencies, the root may
  ask for a conditional T with `getComponent<T>()`: configuring it before `init()` needs a
  reference whether or not it will be enabled. Alternatives: a compile error pointing to a
  new `App::findComponent<T>()` that returns nullptr when disabled (one more API; the
  before-`init()` case would need getComponent anyway); or never throw and let the root use
  a disabled component (which may touch uninitialized state). An interface whose
  providers are `Alternative<I>` (one or several) is resolved at runtime: before `init()`
  it throws (the choice isn't made yet), after it the enabled one, and it throws if none or
  several are enabled, or if a failed `init()` never chose. `run(&C::f)` resolves its
  target after `init()` and before `start()`, so an unchosen alternative throws before
  anything starts; a target that is a single conditional component doesn't compile
  (`RunTargetConditional`), since `run` has no use for it before `init()`. Alternatives:
  return a disabled component anyway (as `framework/` does; using it would touch an
  uninitialized component); or make every root lookup valid only after `init()` (one rule,
  but configuring a component before `init()` would no longer be possible).

- **Root `getComponent<T>()` after a failed `init()`** returns a component that was never
  initialized (or was already deinitialized by the rollback) without throwing
  (pre-existing; only alternatives are checked). Root access could check the registry
  state and the component's own state and throw instead.

- **`enabled()` is asked in init order, in place of `init()`.** So it can use its
  dependencies (typically `Config`). A disabled component's own dependencies are never
  checked (that matters only for its `Optional<I>` on several enabled alternatives).
  Alternative: ask every component before any `init()`; then `enabled()` couldn't use
  dependencies, which is the main use.

- **`keel::Conditional` is an opt-in base class; nothing may require a conditional
  component.** Only components that derive from it are asked `enabled()` (a pure virtual
  `bool enabled() const`). A required dependency on a conditional T (`RequiredConditional`),
  a required I whose one provider is a conditional `Provides<I>`
  (`RequiredConditionalProvider`), and an `All<I>` whose providers are all conditional
  (`OnlyConditionalProviders`) are compile errors; every `Alternative<I>` must be
  conditional (`AlternativeNotConditional`); a private or repeated base is
  `InaccessibleConditional`; a non-const `enabled()` leaves the class abstract
  (`AbstractComponent`, whose message mentions the signature). So the only runtime
  composition failures left are those of `Alternative<I>` (none or several enabled).
  Revisitable:
  - **A conditional component may not require another conditional one** (use
    `Optional<T>`), although both could be off together. Stricter than before (a disabled
    component's dependencies weren't checked), accepted: keel can't know that two
    `enabled()`s agree. Alternative: allow it and check at runtime that a required
    conditional dependency is enabled when its dependent is (the old runtime failure, in
    fewer places).
  - **A base class, not a `Component<...>` list entry.** `AsyncInit` is a list entry
    (`Provides<I>` and `Alternative<I>` are bases too, since 2026-11-08); `Conditional` is
    a base so that its pure virtual forces the right signature (a wrong one is an
    abstract-class error, not a silent non-override). Alternative: a `keel::Conditional`
    tag in the list plus a check that `enabled()` exists with the right signature
    (consistent style, but the check is hand-written and the hook would have to live in
    every component again).
  - **`All<I>` needs one provider that isn't conditional**, so it is never empty. An
    `All<I>` over alternatives only is an error too; `Optional<All<I>>` is the way.
    Alternative: allow it and throw at runtime when every provider is off (the old rule).
  - **`Alternative<I>` needs `Conditional`** rather than implying it (it doesn't derive
    from it), so `enabled()` stays a normal override with the compiler's checks. Checked
    per component through a common base of every `Alternative<I>`, so it needs no
    reference to I; the diagnostic is `AlternativeNotConditional<C>` (it named I while
    `Alternative<I>` was a list entry).
  - **Root access to a conditional component** stays a runtime check (see
    `App::getComponent<T>()` above).

- **Alternatives are checked where they are needed.** When a component that requires I
  initializes, exactly one alternative of I must be enabled; for `Optional<I>`, at most one
  (none gives nullptr); `Optional<All<I>>` takes the enabled ones (`All<I>` over
  alternatives only doesn't compile: `OnlyConditionalProviders`). Two
  enabled alternatives of an I that nothing depends on as one I are not an error.
  Alternative: check every group of alternatives in `init()`, used or not. Not done: the
  dependents are what need one, and `framework/` behaves the same.

- **A single `Alternative<I>` is allowed, and may be required.** It is a provider that
  configuration may turn off, for dependents that require it (a test composition may
  compose just one alternative); a required dependent then fails `init()` ("none of
  its alternatives (...) is enabled"). (A conditional `Provides<I>` can't be required at
  all: `RequiredConditionalProvider`, whose message points to `Alternative<I>` for this.
  Nor can a concrete alternative: `Component<RedisCache>` is `RequiredConditional`; only
  `Alternative<Self>`, required as `Self`, goes through the runtime check.)
  Alternative: require two or more alternatives at compile time. Not done: a stub can be
  added later without touching the first one.

- **Mixing `Provides<I>` and `Alternative<I>` providers is an error** when anything needs
  one I: `MixedProviders<I, TypeList<Provides...>, TypeList<Alternatives...>>`, reported
  once per I however many components, root calls and `run(&I::f)` need it (the per-site
  ambiguity errors are skipped for a mixed I). `AmbiguousDependency` now suggests
  `Alternative<I>`. Alternative:
  the `Provides<I>` provider as the default when no alternative is enabled. Not done: one
  rule ("all providers are alternatives"), and the default can be an alternative too.
  A component that derives from both `Provides<I>` and `Alternative<I>` is
  `ProvidesAndAlternative<C, I>`.

- **A disabled component that looks up a dependency gets `keel::Error`** ("X used Y, but X
  is disabled"), e.g. from a method the root calls on a reference it got before `init()`.
  A disabled component is still constructed and destroyed (keep constructors cheap).

- **`All<I>` leaves out the component itself.** A component that provides I and lists
  `All<I>` (a composite: a HealthCheck that checks all the others) gets every other
  provider, and is ordered after them; "at least one" counts the others. Same rule as
  `InitAfter<I>`. Alternative: report it as a cycle, like `C : Component<I>, Provides<I>`.
  Not obvious consequences, reported only as `DependencyCycle<...>`: two composites of the
  same I each come after the other (compile-fail test `two_composites`); and a subclass of
  a composite (`Y : X` with `X : Component<All<I>>, Provides<I>`) inherits both, so
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
  if the component derives from `Provides<Loop>`, and is otherwise
  `RunTargetNotComposed<Loop, F>`, whose message suggests `Provides<Loop>` or a lambda. Alternative: call it on the one composed
  component that derives from Loop. Not done for the same reason as `UndeclaredProvider`:
  one matching rule everywhere.

- **A generic lambda passed to `run(fn)` is called with the App without checking first.**
  So an error in its body is reported once, by the compiler, instead of a second
  "not callable" error from keel. Trade-off: a generic lambda that can't take the App at
  all (`[](auto x, auto y) {}`) gets the compiler's "no matching function" rather than
  keel's `RunNotCallable`. Non-generic callables are still checked by keel.

- **Large apps on MSVC may need `/constexpr:steps1000000`.** The order is computed at
  compile time; the 150-component test needs about 511k clang constexpr steps (nearly all in
  `topologicalOrder`), above MSVC's default of about 100k. The tests set the flag; documented
  in docs/composition.md and docs/reference.md. Alternative: a cheaper sort.

- **A component in two bundles is a duplicate.** Bundles are expanded in place into one
  list, and a component listed twice is `DuplicateComponent<T>` (its message mentions
  bundles), as in a plain `compose<...>` list. A bundle reached twice (listed twice, or also
  inside another bundle) is one `DuplicateBundle<B>`, not one error per component.
  Alternative: merge duplicates (set semantics), so overlapping bundles
  (`Bundle<Logger, Database>` and `Bundle<Logger, HttpServer>`) compose. Not done: one rule,
  each component once, and an overlap is often a mistake (two bundles that grew into each
  other). Overlapping bundles are written as a shared bundle plus the rest. An empty
  `Bundle<>` is allowed and adds nothing; a list with no components at all is
  `EmptyComposition<>`.

- **A bundle is not a dependency.** Any entry naming a bundle (`Component<Core>`,
  `Optional<Core>`, `InitAfter<Core>`, `All<Core>`) is
  `BundleAsDependency<C, Bundle<...>>`: list the components used. Alternative: treat it as
  "depends on each of them". Not done: a component should name what it uses, and the
  bundle's contents may change without the component noticing.

- **`keel::isValid<...>` is a plain bool; `keel::requireValid<...>` reports.** `isValid`
  runs the checks of `compose<...>` without reporting them, so `static_assert(!isValid<...>)`
  and `if constexpr` work. A positive guard in a header uses `requireValid` (true, or the
  named compile error, via `sizeof(App<...>)`). Alternative: one name with the `framework/`
  meaning ("true or a compile error"), which can't express "this composition is invalid".
  The checks are shared, not duplicated: each check's outcome is one type (a `Passed<...>`
  or the named failure); `compose` derives from the outcomes, `isValid` tests `isPassed<>`
  on the same outcome types.

- **`isValid` covers the composition, not its uses.** It agrees with `compose<...>` on the
  list and the declared dependencies, including a duplicate in a component's own list. It
  doesn't see: misuse inside components (`getComponent<T>()` on an undeclared or optional
  T), what construction and destruction need (constructor arguments, a missing default
  constructor, a private or deleted destructor), and the root's `getComponent<T>()` or
  `run(fn)`. Each compile-fail test that composes something asserts `isValid` (false or
  true) on the same list with the message "isValid disagrees with compose", and the runner
  rejects output that mentions `isValid`.

- **Incomplete types poison later checks.** Whether a type is a component is fixed the first
  time keel asks (`isValid`, `compose`): a guard on a forward-declared component makes it
  "not a component" for the rest of the file, also after its definition (NotAComponent,
  whose message says "or isn't defined at this point"). Documented on `isValid`. A dedicated
  "incomplete" error would need a completeness check that is itself fixed at first use, so
  it can't do better.

- **`App::initOrder()` is a compile-time constant.** A `static constexpr` function returning
  `std::array<std::string_view, N>`: the type names in init order, usable in a
  `static_assert` and without constructing the App. It includes disabled components
  (a `keel::Conditional`'s `enabled()` is asked at runtime); `timings()` shows what ran. Alternative (as in
  `framework/`): a runtime `std::vector` of what was actually initialized. With async
  components it stays the dependency order; the order calls finish in is in `timings()`.

- **Timings are always recorded.** Two `steady_clock::now()` per hook call, and one vector
  reserved by `init()` for every call an App can make (four per component), so recording
  never allocates later, also in `stop()`/`deinit()`, which are `noexcept`. No opt-out, no
  injectable clock. `enabled()` and keel's own dependency checks aren't timed; a disabled
  component has no entries. A call that throws is recorded with `failed = true`, followed
  by the rollback's calls. Alternatives: opt-in (a flag or a template parameter); time
  `enabled()` too; a user-supplied clock. With async components, entries are appended under
  a mutex in the order the calls finish (so an async call can appear after a later one in
  init order). Alternative: per-thread buffers merged at the end (no lock, but an order to
  define).

- **`App::toDot()` draws the ordering edges.** One arrow per edge the init order is derived
  from, from the dependent to the dependency, styled by entry kind (required plain,
  `Optional` dashed, `InitAfter` dotted with an open head, `All` bold, `Optional<All>` bold
  and dashed),
  labelled with the interface when it resolves through one. A dependency on an interface
  with alternatives draws one arrow per alternative; an absent `Optional<T>`/`InitAfter<T>`
  draws nothing. Nodes list the provided interfaces ("provides I", "alternative I"); after
  `init()`, disabled ones are gray and marked "(disabled)", with their arrows. Node ids are
  list positions (`n3`), labels are escaped (`"` and `\`). Built at runtime from
  compile-time tables, only when called: about +0.9 s of compile time at -O2 for the
  150-component test (one non-template loop writes the arrows), nothing when unused. Two
  entries that resolve to the same component (T, and an interface T provides) give two
  arrows; not merged, since each shows one entry. Alternatives: a constexpr string; arrows from
  dependency to dependent (the init direction); leave disabled components out; show the
  missing `Optional` targets as dashed placeholders.

- **Async: one thread per async component and phase, created when the phase begins.** It
  waits for its dependencies itself, so a chain of async components never waits for the
  calling thread. Alternatives: a thread pool (bounded threads, but a waiting step would hold
  a pool thread, or the pool needs a ready queue); or creating the thread when the
  component becomes ready (fewer idle threads, but only the calling thread could do that, so
  an async component behind a busy calling thread would wait for it). Thread names, stack
  sizes and affinity are not configurable.

- **Async: the calling thread runs first what an async component waits for.** Among its
  ready components it picks the first, in init order, that some async component waits for
  (directly or through others), else the first ready one (2026-11-04 decision). No
  priorities by duration or path length. So the relative order of components with no
  dependency path between them is not fixed once some component is async; dependencies
  always are. A phase without async components runs the plain loop in init order, exactly
  as before (an app without any async component never reaches the scheduler: known at
  compile time; nor does one after `setAsync(false)`).

- **Async failure: the earliest failed component in init order is rethrown.** If several
  init()s (or start()s) throw, the others' exceptions are dropped (each failure is still in
  `timings()` with `failed`). Which components get to fail depends on timing, since nothing
  new starts after the first failure, so the rethrown one can differ between runs. Alternatives: the first to fail in time (not reproducible); an
  aggregate exception (changes the "rethrown unchanged" rule). A thread that can't be
  created counts as a failure of its component (`std::system_error`, rethrown by the same
  rule, recorded in `timings()` as a failed call of zero duration); the threads already
  created are told to stop and joined. Tests simulate it through
  `keel::detail::testHooks.beforeThread`, a function pointer in the header (tests only).

- **Async: `start()` still begins after every `init()`.** An `AsyncStart` component can't
  start before an unrelated slow `AsyncInit` finishes (lifecycle guarantee "when your
  start() runs, every component has finished init()"). Alternative: one combined schedule
  per component (init then start as soon as its dependencies have started). Not done:
  weaker guarantee, harder rollback.

- **Async: `enabled()` runs on the component's own thread** with `AsyncInit`, just before
  its `init()`, after its dependencies are done (so it can use them, as for sync ones).
  Alternative: ask every `enabled()` on the calling thread first; then it couldn't use
  dependencies initialized asynchronously.

- **Component and app state are atomic.** keel's own reads of a component's state are
  ordered by the scheduler, but a lookup from a thread the component started itself (in
  `start()`) used to race with keel writing the state. Atomic loads/stores (seq_cst) make
  that well defined. They don't make such lookups meaningful during shutdown (still open:
  design log 2026-11-03, "dependency access from threads during shutdown").

- **`App::setAsync(false)` runs async components synchronously** (debugging, reproducible
  tests, narrowing down a race): everything on the calling thread in init order. Before
  `init()` only (after, `keel::Error`). Not reachable through `keel::run<AppType>`. erocha
  may revisit the name (`setAsync`, `runAllOnCallingThread`, a compose-time option, ...).

- **Async is a property of the whole app, not of one component**: once any component is
  async, components with no dependency path between them may run at the same time, sync ones
  included, so shared dependencies (a Logger) must be thread-safe; init()/start() may run on
  worker threads while stop()/deinit() run on the calling thread. The CMake target links
  `Threads::Threads`; users who copy the header without CMake need `-pthread` on Linux.
  Documented in the README ("Threads") and docs/async.md.

- **Type names first in the compile errors (C++26).** When user-generated `static_assert`
  messages are available, put the type names first in each message ("Migrations needs
  Database, which isn't composed") instead of the template parameter names.

- **Install/export: `find_package(keel CONFIG)`, SameMinorVersion.** Install rules
  (`KEEL_INSTALL`, on when top-level) install `keel.hpp` to `include/` and the exported
  `keel::keel` target with `keelConfig.cmake` (`find_dependency(Threads)`) and
  `keelConfigVersion.cmake` under `share/cmake/keel` (header-only: `ARCH_INDEPENDENT`).
  Version compatibility is `SameMinorVersion`: before 1.0 a minor version may break the API,
  so `find_package(keel 0.1)` accepts 0.1.x only. Alternatives: `SameMajorVersion` (would
  accept 0.2 for 0.1, wrong for 0.x), `AnyNewerVersion` (same problem, worse). Switch to
  `SameMajorVersion` at 1.0. CI installs and builds `tests/install_consumer` against it.

- **`Provides<I>` and `Alternative<I>` are base classes, not `Component<...>` list
  entries** (2026-11-08). The list says what a component needs; its base classes say what
  it is (`Provides<I>`, `Alternative<I>`, like `Conditional`). Listing them is
  `ProvidesAsDependency<C, I>` / `AlternativeAsDependency<C, I>`; deriving from them
  privately, protectedly or twice is `InaccessibleProvides<C, I>` /
  `InaccessibleAlternative<C, I>`; both for one I is `ProvidesAndAlternative<C, I>`.
  Alternative: keep them in the list (one place to read everything a component declares,
  and keel can enumerate what a component provides). Revisitable.

- **Interfaces nothing refers to are invisible to keel.** C++20 can't list the bases of a
  class, so keel asks each composed component "do you provide I?" only for the I that
  something refers to: a dependency of a component, or the root's `getComponent<I>()` /
  `run(&I::f)`. Consequences, compared with `Provides<I>` and `Alternative<I>` in the list:
  - A `Provides<I>` or `Alternative<I>` that nothing refers to isn't checked: no
    `NotDerivedFromInterface`, `InaccessibleProvides`, `InaccessibleAlternative` or
    `ProvidesAndAlternative` for it, and `isValid<C>` is true for such a C on its own. (An
    alternative must still be a `Conditional`: that check doesn't need I.)
  - `Provides<const I>` / `Alternative<const I>` (or any type no entry can name) is a
    compile error in the marker's constructor, so only where the component is constructed;
    `isValid` doesn't see it (it used to be `InvalidDependency`, which `isValid` saw).
  - The root's `getComponent<I>()` on alternatives nothing else refers to builds the
    candidate list itself (on the stack), since nothing bound I at construction.
  - `App::toDot()` labels a component only with the interfaces something refers to, in the
    order keel first meets them (it was the list order).
  - A provider that can't be used as an I is reported once, at the first dependency on I
    in compose order (else at the root), and the other dependencies on I are not checked
    further (they would report a missing or ambiguous I).
  Alternatives: C++26 reflection (`bases_of`) would let keel enumerate the bases and check
  every `Provides<>`; a CRTP marker (`Provides<I, Self>`) could check itself but is
  heavier to write.
  Cost: the bindings for lookups are now built per referred type (not per component), and
  each referred type is asked of every component. The 150-component test in
  `interfaces_test.cpp` takes about 35% more compile time (Apple clang 17, `-fsyntax-only`:
  4.1 s vs 3.0 s CPU); smaller files about 8% more.
