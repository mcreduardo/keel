# Inspecting and testing

[keel](../README.md) › docs › Inspecting and testing

## Checking compositions: `requireValid` and `isValid`

Two compile-time checks of a composition, without constructing anything:

```cpp
using Core = keel::Bundle<Config, Logger>;

static_assert(keel::requireValid<Core, Database, HttpServer>);      // production
static_assert(keel::requireValid<Core, FakeDatabase, HttpServer>);  // the test variant
static_assert(!keel::isValid<Core, HttpServer>);                    // HttpServer needs a Database
```

- **`keel::requireValid<Cs...>`** is `true`, or the compile error `compose<Cs...>` would
  give, naming the mistake (`MissingDependency<HttpServer, Database>`). It is never
  `false`. Use it as a guard in the header next to the compositions: every variant is then
  checked on every build, including variants that no binary of the current build uses.
- **`keel::isValid<Cs...>`** is a plain `constexpr bool`, never a compile error. Use it for
  compositions that must *not* compile (a test of a negative case), or to choose between
  compositions with `if constexpr`. A failing `static_assert(keel::isValid<...>)` only says
  "static assertion failed", so for positive guards prefer `requireValid`.

Both run every check `compose<...>` runs on the list and the declared dependencies: types
that aren't components, duplicates, missing or ambiguous dependencies, cycles, and so on.
They don't see:

- how the components use their dependencies (a `getComponent<T>()` on an undeclared `T`
  fails where the component is compiled, not in the guard);
- what constructing and destroying the components needs: constructor arguments, a missing
  default constructor, a private destructor;
- the root's `getComponent<T>()` and `run(fn)`.

**Every component must be complete where `isValid` (or `requireValid`, or `compose`) first
sees it.** The answer for a type is fixed the first time keel asks: a guard on a
forward-declared component makes it "not a component" for the rest of the translation
unit, also after its definition. Put guards where the component headers are included.

## What an `App` can tell you

```cpp
ServerApp::initOrder();  // std::array<std::string_view, N>: component names in init order
app.timings();           // std::span<const keel::Timing>: how long each lifecycle call took
app.toDot();             // std::string: the dependency graph in Graphviz DOT format
```

### The init order

`initOrder()` is a `static constexpr` function: the order is known at compile time, so a
test can check it without constructing or running anything:

```cpp
using ServerApp = keel::App<HttpServer, Database, Logger, Config>;

static_assert(ServerApp::initOrder()[0] == "Config");
static_assert(ServerApp::initOrder().back() == keel::typeName<HttpServer>());
```

It includes components that configuration may turn off (a `keel::Conditional`'s
`enabled()` is asked at runtime); `timings()` shows what actually ran. Construction
follows the same order; `stop()`, `deinit()` and destruction run in reverse.

**Names** are `keel::typeName<T>()`: the type as the compiler spells it (`Logger`,
`edge::Imu<edge::Front>`). The spelling differs between compilers, for anonymous
namespaces and template arguments for instance (Clang: `(anonymous namespace)::Logger`), so
compare with `keel::typeName<T>()` rather than a literal in portable code. Inside a
component, `componentName()` returns its own name (empty in its constructor, before the
component is part of an App).

### Startup timings

```cpp
app.init();
app.start();
app.stop();
app.deinit();  // first: the destructor's calls can't be seen
for (const keel::Timing& t : app.timings()) {
    std::printf("%-6s %-12.*s %6.1f ms%s\n", keel::toString(t.phase).data(),
                static_cast<int>(t.component.size()), t.component.data(),
                std::chrono::duration<double, std::milli>(t.duration).count(), t.failed ? " (failed)" : "");
}
```

```
init   Config          6.3 ms
init   Logger          3.0 ms
init   Database       40.0 ms
init   Cache          20.9 ms
...
deinit Database       15.0 ms
```

- One `keel::Timing{component, phase, duration, failed}` per `init()`, `start()`, `stop()`
  and `deinit()` call that ran, measured with `std::chrono::steady_clock`, in the order the
  calls finished.
- A call that threw has `failed` set, followed by the rollback's calls.
- A `keel::Conditional`'s `enabled()` isn't timed, and a disabled component has no entries.
- Recording is always on and never allocates after `init()` (which reserves room for
  every call).
- The span holds the entries recorded when you called `timings()`; call it again later for
  more. Call it from the thread that drives the App. With `keel::run`, read it inside
  `fn`, which runs before the shutdown.

### The dependency graph

```cpp
std::ofstream("graph.dot") << app.toDot();  // then: dot -Tsvg graph.dot -o graph.svg
```

One box per component, in init order, listing the interfaces it provides ("provides I",
"alternative I") and its async base classes ("async init", "async start"). Only interfaces that some
component of the app refers to are listed: `keel::Provides<I>` is a base class, and keel
can't list a class's bases, only ask about the interfaces it knows. One arrow per ordering
edge, from a component to the component it comes after; dependencies are drawn to the
right, so init order reads right to left.

- required dependencies are plain arrows;
- `Optional` is dashed;
- `After` is dotted, with an open head;
- `All` is bold, `Optional<All>` bold and dashed;
- an arrow that resolves through an interface is labelled with the interface;
- two entries that resolve to the same component (`T`, and an interface `T` provides) give
  two arrows;
- after `init()`, disabled components are gray and marked "(disabled)"; their arrows are
  still drawn.

`examples/introspection.cpp dot` prints one.

## Testing components

To test a component, compose a **smaller application**: the component, its real
dependencies where convenient, and fakes for the rest. Fakes provide the same interface:

```cpp
class FakeStorage : public keel::Component<>, public keel::Provides<Storage>, public Storage {
public:
    void put(const std::string& key, const std::string&) override { lastKey = key; }
    std::string lastKey;
};

TEST(Service, StoresTheAnswer) {
    auto app = keel::compose<Service, FakeStorage>();
    app.init();
    app.getComponent<Service>().handle();
    EXPECT_EQ(app.getComponent<FakeStorage>().lastKey, "answer");
}
```

The test composition is checked at compile time like any other. If `Service` gains a new
dependency, the test fails to compile and says what is missing. That is why it pays to
depend on [interfaces](dependencies.md#interfaces-keelprovidesi) at the boundaries you want
to fake (storage, network, clock, hardware), or to let a concrete component
[provide itself](dependencies.md#substituting-a-subclass-for-a-component) so a subclass can
replace it.

A component that lists `All<I>` needs at least one provider in the test too; a one-line
fake is enough (`examples/interfaces.cpp` does this with `FakeHealth`).

For tests of async components, `app.setAsync(false)` runs everything on the test's thread,
in init order ([Async startup](async.md#debugging-setasyncfalse)).
