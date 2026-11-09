// compile_errors: the mistakes keel rejects at compile time, one per KEEL_ERROR value.
// Each gives exactly one error, naming the mistake and the types involved. docs/errors.md
// explains each one and its fix.
//
// Build: c++ -std=c++20 -I.. -fsyntax-only -DKEEL_ERROR=6 compile_errors.cpp   (1..35)
// Without KEEL_ERROR it compiles and does nothing.
//
//   The compose<...> list            Interfaces and collections
//     1 NotAComponent                  10 NotDerivedFromInterface
//     2 DuplicateComponent             11 InaccessibleBase
//     3 AbstractComponent              12 AmbiguousDependency
//     4 EmptyComposition               13 MixedProviders
//     5 DuplicateBundle                14 UndeclaredProvider
//                                      15 MissingProvider
//   A component's list                 16 ProvidesAndAlternative
//     6 MissingDependency
//     7 DependencyCycle              Inside a component
//     8 InvalidDependency              17 getComponent<T>() on an undeclared T
//     9 a dependency listed twice      18 getComponent<T>() on keel::Optional<T>
//                                      19 getComponent<T>() on keel::After<T>
//   The composition root               20 findComponent<T>() on a required T
//    23 NotInApp                       21 getAll<I>() on a required I
//    24 AmbiguousInterface             22 stop() without noexcept
//    25 RunTargetNotComposed
//    26 RunTargetAmbiguous           Conditional components
//    27 RunTargetNotCallable           31 RequiredConditional
//    28 RunNotCallable                 32 OnlyConditionalProviders
//    29 NotAnApp                       33 AlternativeNotConditional
//                                      34 enabled() without const (AbstractComponent)
//   Guards                             35 RunTargetConditional
//    30 requireValid (reports the mistake itself)

#include "keel.hpp"

// Shared by the cases below.
class Config : public keel::Component<> {};
class Logger : public keel::Component<Config> {};

struct Storage {  // an interface
    virtual ~Storage() = default;
};
class DiskStorage : public keel::Component<>, public keel::Provides<Storage>, public Storage {};
class MemoryStorage : public keel::Component<>, public keel::Provides<Storage>, public Storage {};

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

#elif KEEL_ERROR == 9  // the same type twice in one list (T and keel::Optional<T>)
class Server : public keel::Component<Logger, keel::Optional<Logger>> {};
void mistake() { keel::compose<Server, Logger, Config>(); }

#elif KEEL_ERROR == 10  // keel::Provides<I> without deriving from I
class Archive : public keel::Component<>, public keel::Provides<Storage> {};
class Service : public keel::Component<Storage> {};
void mistake() { keel::compose<Service, Archive>(); }

#elif KEEL_ERROR == 11  // keel::Provides<I> as a private base (the default for a class)
class S3Storage : public keel::Component<>, keel::Provides<Storage>, public Storage {};
class Service : public keel::Component<Storage> {};
void mistake() { keel::compose<Service, S3Storage>(); }

#elif KEEL_ERROR == 12  // two providers for a dependency on one Storage
class Service : public keel::Component<Storage> {};
void mistake() { keel::compose<Service, DiskStorage, MemoryStorage>(); }

#elif KEEL_ERROR == 13  // keel::Alternative<I> mixed with keel::Provides<I>
class S3Storage : public keel::Component<>, public keel::Alternative<Storage>, public Storage,
                  public keel::Conditional {
    bool enabled() const override { return true; }
};
class Service : public keel::Component<Storage> {};
void mistake() { keel::compose<Service, DiskStorage, S3Storage>(); }

#elif KEEL_ERROR == 14  // derives from the interface, forgot keel::Provides<I>
struct Metrics {
    virtual ~Metrics() = default;
};
class PrometheusMetrics : public keel::Component<>, public Metrics {};
class Server : public keel::Component<Metrics> {};
void mistake() { keel::compose<Server, PrometheusMetrics>(); }

#elif KEEL_ERROR == 15  // keel::All<I> with no provider
struct HealthCheck {
    virtual ~HealthCheck() = default;
};
class HealthEndpoint : public keel::Component<keel::All<HealthCheck>> {};
void mistake() { keel::compose<HealthEndpoint>(); }

#elif KEEL_ERROR == 16  // keel::Provides<I> and keel::Alternative<I> on one component
class S3Storage : public keel::Component<>,
                  public keel::Alternative<Storage>,
                  public keel::Provides<Storage>,
                  public Storage,
                  public keel::Conditional {
    bool enabled() const override { return true; }
};
class Service : public keel::Component<Storage> {};
void mistake() { keel::compose<Service, S3Storage>(); }

#elif KEEL_ERROR == 17  // getComponent<T>() for a T the list doesn't name
class Server : public keel::Component<Logger> {
    void init() override { getComponent<Config>(); }
};

#elif KEEL_ERROR == 18  // getComponent<T>() for an optional dependency
class Server : public keel::Component<keel::Optional<Logger>> {
    void init() override { getComponent<Logger>(); }
};

#elif KEEL_ERROR == 19  // getComponent<T>() for an ordering-only dependency
class Server : public keel::Component<keel::After<Logger>> {
    void init() override { getComponent<Logger>(); }
};

#elif KEEL_ERROR == 20  // findComponent<T>() for a required dependency
class Server : public keel::Component<Logger> {
    void init() override { findComponent<Logger>(); }
};

#elif KEEL_ERROR == 21  // getAll<I>() on a required I (one provider)
class Inventory : public keel::Component<Storage> {
    void init() override { getAll<Storage>(); }
};

#elif KEEL_ERROR == 22  // stop() and deinit() must be noexcept
class Server : public keel::Component<> {
    void stop() override {}
};

#elif KEEL_ERROR == 23  // the root asks for a type the App doesn't have
void mistake() {
    auto app = keel::compose<Logger, Config>();
    app.getComponent<Storage>();
}

#elif KEEL_ERROR == 24  // the root asks for an interface with two providers
void mistake() {
    auto app = keel::compose<DiskStorage, MemoryStorage>();
    app.getComponent<Storage>();
}

#elif KEEL_ERROR == 25  // run(&C::f) on a C that isn't composed
class Cli : public keel::Component<> {
public:
    int execute() { return 0; }
};
void mistake() {
    auto app = keel::compose<Logger, Config>();
    app.run(&Cli::execute);
}

#elif KEEL_ERROR == 26  // run(&I::f) on an interface with two providers
struct Shell {
    virtual ~Shell() = default;
    virtual int serve() = 0;
};
class Terminal : public keel::Component<>, public keel::Provides<Shell>, public Shell {
public:
    int serve() override { return 0; }
};
class Console : public keel::Component<>, public keel::Provides<Shell>, public Shell {
public:
    int serve() override { return 0; }
};
void mistake() {
    auto app = keel::compose<Terminal, Console>();
    app.run(&Shell::serve);
}

#elif KEEL_ERROR == 27  // run(&C::f) with an f that needs arguments
class Cli : public keel::Component<> {
public:
    int execute(int verbosity) { return verbosity; }
};
void mistake() {
    auto app = keel::compose<Cli>();
    app.run(&Cli::execute);
}

#elif KEEL_ERROR == 28  // run(fn) with an fn that takes something other than the App
void mistake() {
    auto app = keel::compose<Logger, Config>();
    app.run([](int exitCode) { return exitCode; });
}

#elif KEEL_ERROR == 29  // keel::run<T> with a T that isn't a keel::App
void mistake() { keel::run<Logger>([] { return 0; }); }

#elif KEEL_ERROR == 30  // a guard in a header: the error is the mistake itself
class Server : public keel::Component<Logger> {};
static_assert(keel::requireValid<Server, Config>);

#elif KEEL_ERROR == 31  // a required dependency on a component configuration may turn off
class Uplink : public keel::Component<Config>, public keel::Conditional {
    bool enabled() const override { return true; }
};
class Dashboard : public keel::Component<Uplink> {};  // keel::Optional<Uplink> is fine
void mistake() { keel::compose<Dashboard, Uplink, Config>(); }

#elif KEEL_ERROR == 32  // keel::All<I> whose providers may all be turned off
struct HealthCheck {
    virtual ~HealthCheck() = default;
};
class DiskCheck : public keel::Component<>, public keel::Provides<HealthCheck>, public HealthCheck,
                  public keel::Conditional {
    bool enabled() const override { return true; }
};
class HealthEndpoint : public keel::Component<keel::All<HealthCheck>> {};
void mistake() { keel::compose<HealthEndpoint, DiskCheck>(); }

#elif KEEL_ERROR == 33  // keel::Alternative<I> without keel::Conditional
class S3Storage : public keel::Component<>, public keel::Alternative<Storage>, public Storage {};
void mistake() { keel::compose<S3Storage>(); }

#elif KEEL_ERROR == 34  // enabled() without const doesn't override keel::Conditional's
class Uplink : public keel::Component<>, public keel::Conditional {
    bool enabled() { return true; }
};
void mistake() { keel::compose<Uplink>(); }

#elif KEEL_ERROR == 35  // run(&C::f) on a component configuration may turn off
class Uplink : public keel::Component<>, public keel::Conditional {
public:
    int send() { return 0; }

private:
    bool enabled() const override { return true; }
};
void mistake() {
    auto app = keel::compose<Uplink>();
    app.run(&Uplink::send);
}

#endif

int main() {}
