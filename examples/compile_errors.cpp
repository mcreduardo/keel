// compile_errors: the mistakes keel rejects at compile time, one per KEEL_ERROR value.
// Each gives exactly one error, naming the mistake and the types involved. docs/errors.md
// explains each one and its fix.
//
// Build: c++ -std=c++20 -I.. -fsyntax-only -DKEEL_ERROR=6 compile_errors.cpp   (1..34)
// Without KEEL_ERROR it compiles and does nothing.
//
//   The compose<...> list               Interfaces and collections
//    1 NotAComponent                     12 NotDerivedFromInterface
//    2 DuplicateComponent                13 AmbiguousDependency
//    3 AbstractComponent                 14 MixedProviders
//    4 EmptyComposition                  15 UndeclaredProvider
//    5 DuplicateBundle                   16 UndeclaredSubclass
//                                        17 MissingProvider
//   A component's list                   18 LeftOutOfAll
//    6 MissingDependency                 19 MisnestedAll
//    7 DependencyCycle
//    8 InvalidDependency               Inside a component
//    9 BundleAsDependency                20 getComponent<T>() on an undeclared T
//   10 MisplacedAsync                    21 getComponent<T>() on keel::Optional<T>
//   11 a dependency listed twice         22 getComponent<T>() on keel::InitAfter<T>
//                                        23 findComponent<T>() on a required T
//   The composition root                 24 getAll<I>() on a required I
//   27 NotInApp                          25 getComponent<keel::AsyncInit>()
//   28 AmbiguousInterface                26 stop() without noexcept
//   29 RunTargetNotComposed
//   30 RunTargetAmbiguous              Guards
//   31 RunTargetNotCallable              34 requireValid (reports the mistake itself)
//   32 RunNotCallable
//   33 NotAnApp

#include "keel.hpp"

// Shared by the cases below.
class Config : public keel::Component<> {};
class Logger : public keel::Component<Config> {};

struct Storage {  // an interface
    virtual ~Storage() = default;
};
class DiskStorage : public keel::Component<keel::Provides<Storage>>, public Storage {};
class MemoryStorage : public keel::Component<keel::Provides<Storage>>, public Storage {};

#if KEEL_ERROR == 1  // a type in compose<...> that isn't a component
struct Settings {};
void mistake() { keel::compose<Settings, Config>(); }

#elif KEEL_ERROR == 2  // a component listed twice (here through two bundles)
using Core = keel::Bundle<Config, Logger>;
using Diagnostics = keel::Bundle<Logger>;
void mistake() { keel::compose<Core, Diagnostics>(); }

#elif KEEL_ERROR == 3  // a component with a pure virtual function
class Sink : public keel::Component<> {
public:
    virtual void write() = 0;
};
void mistake() { keel::compose<Sink>(); }

#elif KEEL_ERROR == 4  // nothing to compose
void mistake() { keel::compose<keel::Bundle<>>(); }

#elif KEEL_ERROR == 5  // a bundle reached twice
using Core = keel::Bundle<Config, Logger>;
using Service = keel::Bundle<Core, DiskStorage>;
void mistake() { keel::compose<Core, Service>(); }

#elif KEEL_ERROR == 6  // a required dependency that isn't composed
class Server : public keel::Component<Logger> {};
void mistake() { keel::compose<Server, Config>(); }  // no Logger

#elif KEEL_ERROR == 7  // a dependency cycle (Outside only depends on it, and isn't named)
class Q;
class R;
class P : public keel::Component<Q> {};
class Q : public keel::Component<R> {};
class R : public keel::Component<P> {};
class Outside : public keel::Component<P> {};
void mistake() { keel::compose<Outside, P, Q, R>(); }

#elif KEEL_ERROR == 8  // an entry that can never match a component
class Metrics : public keel::Component<> {};
class Server : public keel::Component<keel::Optional<const Metrics>> {};
void mistake() { keel::compose<Server, Metrics>(); }

#elif KEEL_ERROR == 9  // a bundle in a component's list
using Core = keel::Bundle<Config, Logger>;
class Server : public keel::Component<Core> {};
void mistake() { keel::compose<Server, Core>(); }

#elif KEEL_ERROR == 10  // keel::AsyncInit inside another marker
class Database : public keel::Component<Config, keel::Optional<keel::AsyncInit>> {};
void mistake() { keel::compose<Database, Config>(); }

#elif KEEL_ERROR == 11  // the same type twice in one list (T and keel::Optional<T>)
class Server : public keel::Component<Logger, keel::Optional<Logger>> {};
void mistake() { keel::compose<Server, Logger, Config>(); }

#elif KEEL_ERROR == 12  // keel::Provides<I> without deriving from I
class Archive : public keel::Component<keel::Provides<Storage>> {};
void mistake() { keel::compose<Archive>(); }

#elif KEEL_ERROR == 13  // two providers for a dependency on one Storage
class Service : public keel::Component<Storage> {};
void mistake() { keel::compose<Service, DiskStorage, MemoryStorage>(); }

#elif KEEL_ERROR == 14  // keel::Alternative<I> mixed with keel::Provides<I>
class S3Storage : public keel::Component<keel::Alternative<Storage>>, public Storage {};
class Service : public keel::Component<Storage> {};
void mistake() { keel::compose<Service, DiskStorage, S3Storage>(); }

#elif KEEL_ERROR == 15  // derives from the interface, forgot keel::Provides<I>
struct Metrics {
    virtual ~Metrics() = default;
};
class PrometheusMetrics : public keel::Component<>, public Metrics {};
class Server : public keel::Component<Metrics> {};
void mistake() { keel::compose<Server, PrometheusMetrics>(); }

#elif KEEL_ERROR == 16  // a subclass of a component T composed in place of T
class Clock : public keel::Component<> {};
class FakeClock : public Clock {};
class Service : public keel::Component<Clock> {};
void mistake() { keel::compose<Service, FakeClock>(); }

#elif KEEL_ERROR == 17  // keel::All<I> with no provider
struct HealthCheck {
    virtual ~HealthCheck() = default;
};
class HealthEndpoint : public keel::Component<keel::All<HealthCheck>> {};
void mistake() { keel::compose<HealthEndpoint>(); }

#elif KEEL_ERROR == 18  // a class that keel::All<I> would silently leave out
struct HealthCheck {
    virtual ~HealthCheck() = default;
};
class Database : public keel::Component<keel::Provides<HealthCheck>>, public HealthCheck {};
class Cache : public keel::Component<>, public HealthCheck {};  // no keel::Provides<HealthCheck>
class HealthEndpoint : public keel::Component<keel::All<HealthCheck>> {};
void mistake() { keel::compose<HealthEndpoint, Database, Cache>(); }

#elif KEEL_ERROR == 19  // keel::All<keel::Optional<I>> instead of keel::Optional<keel::All<I>>
struct Plugin {
    virtual ~Plugin() = default;
};
class PluginMenu : public keel::Component<keel::All<keel::Optional<Plugin>>> {};
void mistake() { keel::compose<PluginMenu>(); }

#elif KEEL_ERROR == 20  // getComponent<T>() for a T the list doesn't name
class Server : public keel::Component<Logger> {
    void init() override { getComponent<Config>(); }
};

#elif KEEL_ERROR == 21  // getComponent<T>() for an optional dependency
class Server : public keel::Component<keel::Optional<Logger>> {
    void init() override { getComponent<Logger>(); }
};

#elif KEEL_ERROR == 22  // getComponent<T>() for an ordering-only dependency
class Server : public keel::Component<keel::InitAfter<Logger>> {
    void init() override { getComponent<Logger>(); }
};

#elif KEEL_ERROR == 23  // findComponent<T>() for a required dependency
class Server : public keel::Component<Logger> {
    void init() override { findComponent<Logger>(); }
};

#elif KEEL_ERROR == 24  // getAll<I>() on a required I (one provider)
class Inventory : public keel::Component<Storage> {
    void init() override { getAll<Storage>(); }
};

#elif KEEL_ERROR == 25  // a marker is not a dependency
class Database : public keel::Component<Config, keel::AsyncInit> {
    void init() override { getComponent<keel::AsyncInit>(); }
};

#elif KEEL_ERROR == 26  // stop() and deinit() must be noexcept
class Server : public keel::Component<> {
    void stop() override {}
};

#elif KEEL_ERROR == 27  // the root asks for a type the App doesn't have
void mistake() {
    auto app = keel::compose<Logger, Config>();
    app.getComponent<Storage>();
}

#elif KEEL_ERROR == 28  // the root asks for an interface with two providers
void mistake() {
    auto app = keel::compose<DiskStorage, MemoryStorage>();
    app.getComponent<Storage>();
}

#elif KEEL_ERROR == 29  // run(&C::f) on a C that isn't composed
class Cli : public keel::Component<> {
public:
    int execute() { return 0; }
};
void mistake() {
    auto app = keel::compose<Logger, Config>();
    app.run(&Cli::execute);
}

#elif KEEL_ERROR == 30  // run(&I::f) on an interface with two providers
struct Shell {
    virtual ~Shell() = default;
    virtual int serve() = 0;
};
class Terminal : public keel::Component<keel::Provides<Shell>>, public Shell {
public:
    int serve() override { return 0; }
};
class Console : public keel::Component<keel::Provides<Shell>>, public Shell {
public:
    int serve() override { return 0; }
};
void mistake() {
    auto app = keel::compose<Terminal, Console>();
    app.run(&Shell::serve);
}

#elif KEEL_ERROR == 31  // run(&C::f) with an f that needs arguments
class Cli : public keel::Component<> {
public:
    int execute(int verbosity) { return verbosity; }
};
void mistake() {
    auto app = keel::compose<Cli>();
    app.run(&Cli::execute);
}

#elif KEEL_ERROR == 32  // run(fn) with an fn that takes something other than the App
void mistake() {
    auto app = keel::compose<Logger, Config>();
    app.run([](int exitCode) { return exitCode; });
}

#elif KEEL_ERROR == 33  // keel::run<T> with a T that isn't a keel::App
void mistake() { keel::run<Logger>([] { return 0; }); }

#elif KEEL_ERROR == 34  // a guard in a header: the error is the mistake itself
class Server : public keel::Component<Logger> {};
static_assert(keel::requireValid<Server, Config>);

#endif

int main() {}
