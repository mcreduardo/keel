// runtime_choices: decisions that depend on configuration, inside a composition that is
// checked at compile time.
//
//   a plain component that creates the configured implementation in init()   (Compression)
//   keel::Conditional        a component that configuration may turn off      (Telemetry)
//   keel::Alternative<I>     several implementations composed, one enabled    (RedisCache, MemoryCache)
//   keel::run<App>(&C::f)    init, start, call a component's function, stop, deinit
//
// Build: c++ -std=c++20 -I.. runtime_choices.cpp -o runtime_choices     (Linux: add -pthread)
// Run:   ./runtime_choices                       memory cache, no compression, no telemetry
//        ./runtime_choices --redis --zip --telemetry
//        ./runtime_choices --redis --memory      both enabled: keel::Error, exit code 1

#include "keel.hpp"

#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>

class Flags : public keel::Component<> {
public:
    Flags(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            redis = redis || std::strcmp(argv[i], "--redis") == 0;
            memory = memory || std::strcmp(argv[i], "--memory") == 0;
            zip = zip || std::strcmp(argv[i], "--zip") == 0;
            telemetry = telemetry || std::strcmp(argv[i], "--telemetry") == 0;
        }
        memory = memory || !redis;  // the default
    }
    bool redis = false;
    bool memory = false;
    bool zip = false;
    bool telemetry = false;
};

// 1. The first choice: implementations that need the same dependencies (here none) are
// plain classes, and one component creates the configured one.
struct Compressor {
    virtual ~Compressor() = default;
    virtual const char* name() const = 0;
};
class ZipCompressor : public Compressor {
public:
    const char* name() const override { return "zip"; }
};
class NoCompressor : public Compressor {
public:
    const char* name() const override { return "no"; }
};

class Compression : public keel::Component<Flags, keel::Provides<Compressor>>, public Compressor {
public:
    const char* name() const override { return impl_->name(); }  // forwards to the chosen one

private:
    void init() override {
        if (getComponent<Flags>().zip) {
            impl_ = std::make_unique<ZipCompressor>();
        } else {
            impl_ = std::make_unique<NoCompressor>();
        }
    }
    std::unique_ptr<Compressor> impl_;
};

// 2. Implementations that are components of their own (different dependencies, their own
// lifecycle): each is a keel::Alternative<Cache> and a keel::Conditional, and
// configuration enables one.
struct Cache {
    virtual ~Cache() = default;
    virtual const char* kind() const = 0;
};

class RedisCache : public keel::Component<Flags, keel::Alternative<Cache>>, public Cache, public keel::Conditional {
public:
    const char* kind() const override { return "redis"; }

private:
    // Asked once, just before init(). Dependencies are initialized by then.
    bool enabled() const override { return getComponent<Flags>().redis; }
};

class MemoryCache : public keel::Component<Flags, keel::Alternative<Cache>>, public Cache, public keel::Conditional {
public:
    const char* kind() const override { return "memory"; }

private:
    bool enabled() const override { return getComponent<Flags>().memory; }
};

// 3. A feature that may be off. Disabled, none of its hooks run. Nothing may require it:
// a component that lists plain Telemetry doesn't compile; keel::Optional<Telemetry> does.
class Telemetry : public keel::Component<Flags>, public keel::Conditional {
public:
    void count(const char* what) const { std::printf("  telemetry: %s\n", what); }

private:
    bool enabled() const override { return getComponent<Flags>().telemetry; }
};

// Uses the enabled Cache, the Compressor, and Telemetry if it is enabled.
class Service : public keel::Component<Cache, Compressor, keel::Optional<Telemetry>> {
public:
    int serve() {
        std::printf("serving with the %s cache and %s compression\n", cache_->kind(), compressor_->name());
        if (telemetry_ != nullptr) telemetry_->count("request");
        return 0;
    }

private:
    void init() override {
        cache_ = &getComponent<Cache>();            // the enabled alternative
        compressor_ = &getComponent<Compressor>();  // always there
        telemetry_ = findComponent<Telemetry>();    // nullptr when disabled
    }

    const Cache* cache_ = nullptr;
    const Compressor* compressor_ = nullptr;
    const Telemetry* telemetry_ = nullptr;
};

using ServiceApp = keel::App<Service, Telemetry, RedisCache, MemoryCache, Compression, Flags>;

int main(int argc, char** argv) try {
    return keel::run<ServiceApp>(&Service::serve, argc, argv);
} catch (const std::exception& e) {
    // --redis --memory: "keel: Service uses Cache: several of its alternatives are
    // enabled (only one may be): RedisCache, MemoryCache"
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
}
