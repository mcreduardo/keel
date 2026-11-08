// introspection: what an App can tell you about itself.
//
//   App::initOrder()   the init order, a compile-time constant
//   App::timings()     how long each init/start/stop/deinit call took
//   App::toDot()       the dependency graph, for Graphviz
//
// Build: c++ -std=c++20 -I.. introspection.cpp -o introspection     (Linux: add -pthread)
// Run:   ./introspection
//        ./introspection dot | dot -Tsvg -o graph.svg

#include "keel.hpp"

#include <chrono>
#include <cstdio>
#include <exception>
#include <string_view>
#include <thread>

struct HealthCheck {
    virtual ~HealthCheck() = default;
    virtual bool healthy() const = 0;
};

void work(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

class Config : public keel::Component<> {
    void init() override { work(5); }
};

class Logger : public keel::Component<Config> {
    void init() override { work(2); }
};

class Database : public keel::Component<Config, Logger>, public keel::Provides<HealthCheck>, public HealthCheck {
public:
    bool healthy() const override { return true; }

private:
    void init() override { work(30); }  // connect
    void deinit() noexcept override { work(10); }
};

class Cache : public keel::Component<Logger, keel::Optional<Database>>, public keel::Provides<HealthCheck>,
              public HealthCheck {
public:
    bool healthy() const override { return true; }

private:
    void init() override { work(15); }
};

class Metrics : public keel::Component<Logger, keel::After<Database>> {};

class HealthEndpoint : public keel::Component<keel::All<HealthCheck>, Logger> {
    void start() override { work(1); }
    void stop() noexcept override { work(1); }
};

using Service = keel::App<HealthEndpoint, Metrics, Cache, Database, Logger, Config>;

// The order is known at compile time: check it without running anything.
static_assert(Service::initOrder()[0] == "Config");
static_assert(Service::initOrder()[1] == keel::typeName<Logger>());
static_assert(Service::initOrder().back() == "HealthEndpoint");

int main(int argc, char** argv) try {
    Service app;

    if (argc > 1 && std::string_view(argv[1]) == "dot") {
        std::fputs(app.toDot().c_str(), stdout);  // usable before init(), too
        return 0;
    }

    std::printf("init order:");
    for (std::string_view name : Service::initOrder()) std::printf(" %.*s", static_cast<int>(name.size()), name.data());
    std::printf("\n");

    app.init();
    app.start();
    app.stop();
    app.deinit();  // before reading the timings: the destructor's calls can't be seen

    // Real measurements (steady_clock): they differ from run to run, and sleep_for may
    // take longer than asked.
    std::printf("timings:\n");
    for (const keel::Timing& t : app.timings()) {
        const double ms = std::chrono::duration<double, std::milli>(t.duration).count();
        std::printf("  %-6.*s %-15.*s %6.1f ms%s\n", static_cast<int>(keel::toString(t.phase).size()),
                    keel::toString(t.phase).data(), static_cast<int>(t.component.size()), t.component.data(), ms,
                    t.failed ? "  (failed)" : "");
    }
} catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
}
