# Patterns

[keel](../README.md) › docs › Patterns

Recipes for situations keel deliberately has no feature for. Each one is plain C++ on top
of the existing markers.

## Running until SIGINT or SIGTERM

A service runs until it is told to stop. `run(fn)` does `init()` and `start()`, calls `fn`,
and runs `stop()` and `deinit()` **after `fn` returns**. So `fn` waits for the signal:

```cpp
#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

namespace {
std::atomic<bool> shutdownRequested{false};  // lock-free: safe to set from a signal handler

extern "C" void onSignal(int) { shutdownRequested = true; }
}  // namespace

void waitForShutdownSignal() {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    while (!shutdownRequested) std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

int main(int argc, char** argv) try {
    auto app = keel::compose<HttpServer, Router, Database, Logger, Config>(argc, argv);
    app.run(waitForShutdownSignal);  // the components serve on their own threads meanwhile
} catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
}
```

The components do their work on threads they start in `start()` and join in `stop()`
([below](#work-that-runs-until-shutdown)). A program whose main thread does the work (a
frame loop, say) checks the flag in its loop instead: `app.run(&Game::loop)`. On POSIX,
`sigwait` on a blocked signal set is an alternative to polling the flag.

## Several instances of the same class

A component type exists once per application. For several instances, make the component a
template and give each instance a tag. The tag can carry the instance's settings:

```cpp
struct FrontImu { static constexpr std::uint8_t address = 0x68; };
struct RearImu { static constexpr std::uint8_t address = 0x69; };

template <class Position>
class Mpu6050 : public keel::Component<Bus>, public keel::Provides<Sensor>, public Sensor {
    void init() override { open(Position::address); }
    void open(std::uint8_t address);
};

class Recorder : public keel::Component<keel::All<Sensor>> { /* records every sensor */ };

auto app = keel::compose<Mpu6050<FrontImu>, Mpu6050<RearImu>, Recorder, Bus>();
```

Each instantiation is its own component type, with its own name in
[`initOrder()`](inspection-and-testing.md#the-init-order) (`Mpu6050<FrontImu>`).

## Picking one of several providers: role interfaces

With two IMUs composed, a plain `Imu` dependency is ambiguous. A component that needs a
*specific* one depends on a **role**: a small interface template derived from the
interface. Each sensor provides both the interface and its role:

```cpp
struct Imu {
    virtual ~Imu() = default;
    virtual Vector3 acceleration() const = 0;
};

template <class Mount>
struct MountedImu : Imu {};  // "the IMU at this mount"

template <class Mount>
class Bmi088 : public keel::Component<SpiBus>, public keel::Provides<Imu>, public keel::Provides<MountedImu<Mount>>,
               public MountedImu<Mount> {
public:
    Vector3 acceleration() const override;
};

class Odometry : public keel::Component<MountedImu<Front>> { /* ... */ };  // exactly the front one
class Fusion : public keel::Component<keel::All<Imu>> { /* ... */ };       // all of them, at least one

auto app = keel::compose<Bmi088<Front>, Bmi088<Rear>, Odometry, Fusion, SpiBus>();
```

A robot variant without a front IMU then fails to compile, naming `Odometry` and
`MountedImu<Front>`. A simulator provides `SimImu<Front>` with the same roles, and
`Odometry` doesn't change.

## Switching a feature off with a null object

When dependents require an interface and the feature must be off in some configurations,
provide a do-nothing implementation of the interface (drop uploads, skip metrics), and let
one component create the real one or the do-nothing one from configuration. Dependents
keep calling the interface either way. The example, and when to use
`keel::Alternative<I>` instead, are in
[Decisions at runtime](runtime-decisions.md#turning-a-feature-off-which-tool-to-use).

## Fakes for tests

A fake implements the same interface as the real component, and a test composes it instead
([Testing components](inspection-and-testing.md#testing-components)). For a concrete
component without an interface, let it provide itself; then a subclass can replace it:

```cpp
class Clock : public keel::Component<>, public keel::Provides<Clock> {
public:
    virtual std::chrono::system_clock::time_point now() const { return std::chrono::system_clock::now(); }
};

class FakeClock : public Clock {
public:
    std::chrono::system_clock::time_point now() const override { return fixed; }
    std::chrono::system_clock::time_point fixed{};
};
```

## Work that runs until shutdown

`start()` must return. A component that serves, polls or consumes until shutdown starts its
own thread in `start()` and joins it in `stop()`:

```cpp
class Poller : public keel::Component<Device, Logger> {
    void init() override { device_ = &getComponent<Device>(); }
    void start() override {
        thread_ = std::jthread([this](std::stop_token stop) {
            while (!stop.stop_requested()) device_->poll();
        });
    }
    void stop() noexcept override {
        thread_.request_stop();
        thread_.join();  // before Device is stopped: the thread may still use it until here
    }

    Device* device_ = nullptr;
    std::jthread thread_;
};
```

Joining in `stop()` keeps the thread's use of `Device` safe: `stop()` runs before any of
the component's dependencies is stopped or deinitialized.

## Hot paths without virtual calls

Interfaces cost a virtual call. Where that matters, depend on concrete types, and make the
dependent a template so that variants can plug in different types:

```cpp
template <class Gateway>
class Strategy : public keel::Component<OrderBook, Gateway> { /* ... */ };

using Live = keel::App<Common, ExchangeGateway, Strategy<ExchangeGateway>>;
using Replay = keel::App<Common, SimulatedExchange, Strategy<SimulatedExchange>>;
```

Each instantiation is checked by its own composition. And resolve dependencies in `init()`:
a lookup on every call would cost more than the virtual call.

## Build variants and subcommands

Production, simulator and test builds, or one composition per command-line subcommand, are
different `keel::App` types chosen in `main()`, each with a guard. See
[Several compositions in one program](composition.md#several-compositions-in-one-program).

## Per-request or short-lived objects

They are not components. Components create and own them: a request handler creates a
transaction per request, a frame loop creates per-frame state.

## One header per component

Put each component in its own header, with its dependencies on the class line
(forward-declared). The header then documents what the component needs, and code review
shows every new dependency.
