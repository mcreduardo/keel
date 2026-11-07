// runtime_choices: decisions that depend on configuration, inside a composition that is
// checked at compile time.
//
//   enabled()                a component opts out at runtime
//   keel::Alternative<I>     several implementations composed, one enabled at runtime
//   keel::run<App>(&C::f)    init, start, call a component's function, stop, deinit
//
// Build: c++ -std=c++20 -I.. runtime_choices.cpp -o runtime_choices     (Linux: add -pthread)
// Run:   ./runtime_choices                       memory cache, no telemetry
//        ./runtime_choices --redis --telemetry
//        ./runtime_choices --redis --memory      both enabled: keel::Error, exit code 1

#include "keel.hpp"

#include <cstdio>
#include <cstring>
#include <exception>

class Flags : public keel::Component<> {
public:
    Flags(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            redis = redis || std::strcmp(argv[i], "--redis") == 0;
            memory = memory || std::strcmp(argv[i], "--memory") == 0;
            telemetry = telemetry || std::strcmp(argv[i], "--telemetry") == 0;
        }
        memory = memory || !redis;  // the default
    }
    bool redis = false;
    bool memory = false;
    bool telemetry = false;
};

struct Cache {
    virtual ~Cache() = default;
    virtual const char* kind() const = 0;
};

// Two alternatives for Cache: both are composed, configuration enables one.
class RedisCache : public keel::Component<Flags, keel::Alternative<Cache>>, public Cache {
public:
    const char* kind() const override { return "redis"; }

private:
    // Asked once, just before init(). Dependencies are initialized by then.
    bool enabled() const override { return getComponent<Flags>().redis; }
};

class MemoryCache : public keel::Component<Flags, keel::Alternative<Cache>>, public Cache {
public:
    const char* kind() const override { return "memory"; }

private:
    bool enabled() const override { return getComponent<Flags>().memory; }
};

// A feature that may be off. Disabled, none of its hooks run.
class Telemetry : public keel::Component<Flags> {
public:
    void count(const char* what) const { std::printf("  telemetry: %s\n", what); }

private:
    bool enabled() const override { return getComponent<Flags>().telemetry; }
};

// Uses the enabled Cache, and Telemetry if it is enabled.
class Service : public keel::Component<Cache, keel::Optional<Telemetry>> {
public:
    int serve() {
        std::printf("serving with the %s cache\n", cache_->kind());
        if (telemetry_ != nullptr) telemetry_->count("request");
        return 0;
    }

private:
    void init() override {
        cache_ = &getComponent<Cache>();         // the enabled alternative
        telemetry_ = findComponent<Telemetry>();  // nullptr when disabled
    }

    const Cache* cache_ = nullptr;
    const Telemetry* telemetry_ = nullptr;
};

using ServiceApp = keel::App<Service, Telemetry, RedisCache, MemoryCache, Flags>;

int main(int argc, char** argv) try {
    return keel::run<ServiceApp>(&Service::serve, argc, argv);
} catch (const std::exception& e) {
    // --redis --memory: "keel: Service uses Cache: several of its alternatives are
    // enabled (only one may be): RedisCache, MemoryCache"
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
}
