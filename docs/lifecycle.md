# Lifecycle

[keel](../README.md) › docs › Lifecycle

## The four hooks

Every component has four optional hooks: protected virtual functions of keel's component
base class. Override the ones you need; they can be private in your class. Mark them `override`:
without it, a hook with a different signature compiles and is never called.

```cpp
class HttpServer : public keel::Component<Router, Config, Logger> {
    void init() override;             // acquire and configure: bind the socket, read settings
    void start() override;            // begin activity: accept connections, start threads
    void stop() noexcept override;    // end activity: stop accepting, drain requests
    void deinit() noexcept override;  // release: close the socket
};
```

The application runs them in this order:

```
construct every component     (dependency order; constructors can't use dependencies)
init()   of each component    (dependency order)
start()  of each component    (dependency order, after every init() has finished)
          ... the application runs ...
stop()   of each component    (reverse order)
deinit() of each component    (reverse order)
destroy every component       (reverse order)
```

Why two pairs? `init`/`deinit` is about **resources**, `start`/`stop` about **activity**. A
web server opens its socket in `init()` but accepts connections only in `start()`, once
everything else is ready. On shutdown it stops accepting before the database it uses is
stopped. Most components only need `init()`, and maybe `deinit()`.

**`stop()` and `deinit()` are `noexcept`**, like a destructor: shutdown has nobody to
report a failure to. An override without `noexcept` doesn't compile, and an exception that
escapes one ends the program (`std::terminate`). Handle or log cleanup failures inside.

A component that derives from `keel::Conditional` can be turned off at runtime by its
`enabled()`; a disabled component runs none of these hooks. See
[Decisions at runtime](runtime-decisions.md).

## Guarantees

- **When your `init()` runs,** every component you depend on has finished `init()`.
- **When your `start()` runs,** *every* component in the application has finished `init()`,
  and every component you depend on has finished `start()`.
- **When your `stop()` runs,** every component that depends on you has already stopped.
- **When your `deinit()` runs,** every component that depends on you is deinitialized, and
  your dependencies are still initialized: you can still use them (log "closing").

"Depend on" covers required dependencies, `Optional<T>` and `After<T>` when `T` is
composed, every provider of an `All<I>`, and every `Alternative<I>` of an interface `I` you
depend on. Components with no dependency path between
them keep the order of the `compose<...>` list, but don't rely on that: if the order
matters, declare it (`After<T>` if there is no real use).

These guarantees also hold when components run on threads of their own: see
[Async startup](async.md).

## When dependencies are usable

| From | `getComponent`, `findComponent`, `getAll` |
|---|---|
| the constructor | throw `keel::Error` (not part of an application yet) |
| `enabled()` (a `keel::Conditional`) | work; for a dependency on `keel::Alternative<>`s, keel hasn't checked the choice yet (none or several enabled throws `keel::Error`) |
| `init()`, `start()`, `stop()`, `deinit()` | work |
| after your own `deinit()` | throw `keel::Error` |

Resolve dependencies in `init()` and keep the references. A component's own threads (started
in `start()`) may use them until `stop()` joins those threads.

## Failures during startup

Throw from `init()` or `start()` to abort startup. keel then rolls back:

1. stops the components already started, in reverse order;
2. deinitializes the components already initialized, in reverse order;
3. rethrows your exception **unchanged**.

The component that threw gets neither `stop()` nor (if `init()` threw) `deinit()`: undo its
partial work before throwing. If its `start()` threw, its `deinit()` still runs.

```cpp
void Database::init() {
    if (!connect(url_)) throw std::runtime_error("cannot connect to " + url_);
}
```

```cpp
try {
    app.run(serve);
} catch (const std::exception& e) {
    std::fprintf(stderr, "startup failed: %s\n", e.what());  // "cannot connect to ..."
    return 1;
}
```

After a failed startup the `App` is done: every component is deinitialized (but not
destroyed until the `App` is), `init()` or `start()` again throws `keel::Error`, and
`stop()`/`deinit()` do nothing. `examples/lifecycle.cpp --fail` shows a rollback.

## Driving the lifecycle

**The short way: `run`.** It does `init()`, `start()`, your code, `stop()` and `deinit()`,
and shuts down even if your code throws. It returns what your code returns:

```cpp
using ServerApp = keel::App<HttpServer, Router, Database, Logger, Config>;
ServerApp app(argc, argv);

app.run([] { waitForShutdownSignal(); });                                  // a callable with no arguments
app.run([](ServerApp& running) { running.getComponent<Router>(); });       // or one that takes the App
app.run(&HttpServer::serveForever);                                        // or a component's member function
```

(Each line shows one form; an `App` runs once. `waitForShutdownSignal` is defined in
[Patterns](patterns.md#running-until-sigint-or-sigterm).)

- `stop()` and `deinit()` run **after `fn` returns** (or throws). A service's `fn` waits
  for the shutdown signal; the components work on their own threads meanwhile.

- `run(&C::f)` calls `f` on the component `C`, or on the one component that provides the
  interface `C`. `f` must be accessible where you write `&C::f` (public, for `main()`). It resolves `C` after `init()` and before `start()`, so a disabled `C`
  throws `keel::Error` before anything starts.
- A member function inherited from a base class has the base's type (`&Server::serve` may
  be a `void (Loop::*)()`), so it works only if the component provides `Loop`; otherwise
  use a lambda.
- A callable that accepts both no arguments and the App gets the App. Name the App type
  (`using ServerApp = ...`) to write the parameter without templates.
- `return app.run(&Cli::execute);` works as an exit code. A reference returned by `fn`
  refers to a component that is already deinitialized when `run` returns.
- `keel::run<AppType>(fn, args...)` constructs the App from `args` and runs it in one call
  (see [Composing an application](composition.md#several-compositions-in-one-program)). It
  returns by value.

**The explicit way:**

```cpp
app.init();
app.start();
waitForShutdownSignal();  // see Patterns
app.stop();
app.deinit();  // the destructor does it too
```

- `init()` and `start()` run once per App. Calling `init()` twice, `start()` without a
  successful `init()`, `start()` again after `stop()`, or `run()` after `init()` throws
  `keel::Error`. There is no restart: create a new App.
- `stop()` does nothing unless the app is started; `deinit()` stops first if needed, and
  does nothing the second time. Both are `noexcept`.
