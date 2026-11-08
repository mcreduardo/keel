# Async startup

[keel](../README.md) › docs › Async startup

## Single-threaded by default

**By default, everything runs on one thread**: the thread that calls `init()`, `start()`,
`stop()` and `deinit()` on the App, usually the main thread. Components are initialized one
after another, in init order. That is predictable, and it is what windows, GPU contexts
and many SDKs require.

## Opting in: `keel::AsyncInit` and `keel::AsyncStart`

A component whose `init()` or `start()` is slow (connecting to a database, warming a cache,
joining multicast groups, loading a model) can run it **on a thread of its own**:

```cpp
class Database : public keel::Component<Config, Logger, keel::AsyncInit> {
    void init() override;  // connects: runs on its own thread
};

class FeedHandler : public keel::Component<Config, keel::AsyncStart> {
    void start() override;  // joins multicast groups: runs on its own thread
};
```

| Marker | Effect |
|---|---|
| `keel::AsyncInit` | `init()` (and `enabled()`, for a `keel::Conditional`) run on a thread of their own |
| `keel::AsyncStart` | `start()` runs on a thread of its own |

List a marker once, on its own: not inside `Optional<...>` or another marker
(`MisplacedAsync`), and not in `compose<...>`.

## How the scheduling works

- **An async component waits on its own thread.** Each one gets a thread when the phase
  begins, and runs as soon as every component it is ordered after has finished. A chain
  of async components never waits for the calling thread.
- **The calling thread doesn't wait if it has other work.** It goes through the other
  components in init order. It skips those still waiting for an async component and comes
  back to them later. It blocks only when nothing is ready.
- **It runs first what an async component waits for.** Among the ready components, it
  picks one that some async component waits for (directly or through others), so nothing
  unrelated delays the slow one.
- **Dependency order always holds.** A component, async or not, never runs before the
  components it is ordered after have finished, and everything their hooks did is visible
  to it.
- **`start()` begins only after every `init()` has finished**, async ones included.
- **`stop()` and `deinit()` always run on the calling thread**, in reverse order.
- `init()` and `start()` return only when every thread has finished.

So components with no dependency path between them may run in a different order than
without async. For the same reason, a hook that calls `App::getComponent<T>()` on the App
itself (rather than its own `getComponent`) gets an answer that depends on timing, for any
`T` it isn't ordered after. `App::initOrder()` stays the dependency order; `App::timings()` shows the
order in which the calls finished.

From `examples/async.cpp`: `Database` and `Cache` take about 100 ms each and are
`AsyncInit`. The main thread initializes `AuditLog` meanwhile and comes back for `Metrics`
and `HttpServer` once their async dependencies are done:

```
init   Config     (main thread)
init   AuditLog   (main thread)    <- while Database and Cache connect
init   Cache      (async thread)
init   Database   (async thread)
init   Metrics    (main thread)    <- after Database: came back for it
init   HttpServer (main thread)
```

## What async requires of you

**Async is a property of the whole application, not of one component.** Once any
component is async, any two components with no dependency path between them may run at the
same time, *synchronous ones included*. So:

- **Shared dependencies must be thread-safe.** A `Logger` or a metrics registry that
  unrelated components use from their `init()` can be called from two threads at once.
  So can any other state they share (globals, singletons in libraries).
- **Thread affinity.** An async `init()` or `start()` runs on a worker thread, while
  `stop()` and `deinit()` run on the thread that drives the App. Don't rely on
  `thread_local` state, thread-bound contexts (OpenGL, CUDA, some SDKs), or locks held
  from `init()` to `deinit()`.
- **`start()` must return.** An `AsyncStart` component's thread ends when `start()` returns,
  and `App::start()` waits for it. For work that runs until shutdown, start your own thread
  in `start()` and join it in `stop()`. Joining there also keeps that thread's lookups
  safe: `stop()` runs before any of your dependencies is stopped or deinitialized.

## Failures

If an `init()` (or a `keel::Conditional`'s `enabled()`, or `start()`) throws:

- nothing new starts, and the calls already running finish;
- everything initialized (or started) is rolled back in reverse order, on the calling
  thread (see [Lifecycle](lifecycle.md#failures-during-startup));
- the exception of the **earliest failed component in init order** is rethrown. The others
  are dropped; each failure is in `App::timings()` with `failed` set. Which components get
  to fail depends on timing.

A thread that can't be created counts as a failure of its component
(`std::system_error`).

## Debugging: `setAsync(false)`

```cpp
ServerApp app;
app.setAsync(false);  // before init(); afterwards it throws keel::Error
app.init();           // everything on this thread, in init order
```

It runs every component on the calling thread, as if none listed `AsyncInit` or
`AsyncStart`: to debug, to reproduce a run, or to narrow down a race. `keel::run<AppType>`
doesn't offer it; construct the App yourself.

## Threads and linking

keel creates one thread per async component and phase, when the phase begins (no pool).
Thread names, stack sizes and affinity are not configurable.

The CMake target `keel::keel` links `Threads::Threads`. Without CMake, on Linux, compile and
link with `-pthread`. An application without async components never creates a thread.
