// interfaces: depending on interfaces instead of concrete components.
//
//   public keel::Provides<I>        a base class: this component implements the interface I
//   I                               in the list: the one provider of I (two: compile error)
//   keel::All<I>                    every provider of I, at least one
//   keel::Optional<keel::All<I>>    every provider of I, possibly none
//
// The build variant is chosen in the composition root, and the "test" composes a smaller
// app with fakes. A component that provides itself (Clock) can be replaced by a subclass.
//
// Build: c++ -std=c++20 -I.. interfaces.cpp -o interfaces     (Linux: add -pthread)
//        add -DUSE_MEMORY_STORAGE for the other storage
// Run:   ./interfaces

#include "keel.hpp"

#include <cstdio>
#include <exception>
#include <string>
#include <vector>

// Interfaces: plain abstract classes, not components.
struct Storage {
    virtual ~Storage() = default;
    virtual void put(const std::string& key, const std::string& value) = 0;
    virtual std::string describe() const = 0;
};

struct HealthCheck {
    virtual ~HealthCheck() = default;
    virtual bool healthy() const = 0;
    virtual std::string name() const = 0;
};

struct Plugin {
    virtual ~Plugin() = default;
    virtual std::string name() const = 0;
};

// A component that provides itself: its subclasses provide Clock too, so a test can
// compose a FakeClock in its place.
class Clock : public keel::Component<>, public keel::Provides<Clock> {
public:
    virtual long now() const { return 1700000000; }
};

// Two implementations of Storage; an app composes one of them. DiskStorage is also a
// HealthCheck.
class DiskStorage : public keel::Component<>, public keel::Provides<Storage>, public keel::Provides<HealthCheck>,
                    public Storage,
                    public HealthCheck {
public:
    void put(const std::string& key, const std::string&) override { std::printf("  disk: put %s\n", key.c_str()); }
    std::string describe() const override { return "disk"; }
    bool healthy() const override { return true; }
    std::string name() const override { return "disk"; }
};

class MemoryStorage : public keel::Component<>, public keel::Provides<Storage>, public Storage {
public:
    void put(const std::string& key, const std::string&) override { std::printf("  memory: put %s\n", key.c_str()); }
    std::string describe() const override { return "memory"; }
};

class Network : public keel::Component<>, public keel::Provides<HealthCheck>, public HealthCheck {
public:
    bool healthy() const override { return true; }
    std::string name() const override { return "network"; }
};

class Compression : public keel::Component<>, public keel::Provides<Plugin>, public Plugin {
public:
    std::string name() const override { return "compression"; }
};

// Depends on whichever Storage is composed, on every HealthCheck (at least one: a service
// with nothing to report health about is a composition mistake), and on any plugins.
class Service : public keel::Component<Storage, Clock, keel::All<HealthCheck>, keel::Optional<keel::All<Plugin>>> {
public:
    void handle() {
        storage_->put("answer@" + std::to_string(clock_->now()), "42");
        for (const HealthCheck* check : checks_) {
            std::printf("  health %-8s %s\n", check->name().c_str(), check->healthy() ? "ok" : "FAIL");
        }
        std::printf("  %zu plugin(s)\n", plugins_.size());
    }

private:
    void init() override {
        storage_ = &getComponent<Storage>();
        clock_ = &getComponent<Clock>();
        checks_ = getAll<HealthCheck>();  // never empty
        plugins_ = getAll<Plugin>();      // possibly empty
        std::printf("  service: using %s storage\n", storage_->describe().c_str());
    }

    Storage* storage_ = nullptr;
    const Clock* clock_ = nullptr;
    std::vector<HealthCheck*> checks_;
    std::vector<Plugin*> plugins_;
};

// The build variant is chosen in the composition root, not inside the components.
#ifdef USE_MEMORY_STORAGE
using SelectedStorage = MemoryStorage;
#else
using SelectedStorage = DiskStorage;
#endif

// Test fakes: the same interfaces, recording the calls.
class FakeStorage : public keel::Component<>, public keel::Provides<Storage>, public Storage {
public:
    void put(const std::string& key, const std::string&) override { lastKey = key; }
    std::string describe() const override { return "fake"; }
    std::string lastKey;
};

class FakeHealth : public keel::Component<>, public keel::Provides<HealthCheck>, public HealthCheck {
public:
    bool healthy() const override { return true; }
    std::string name() const override { return "fake"; }
};

class FakeClock : public Clock {  // provides Clock: inherited from Clock's list
public:
    long now() const override { return 42; }
};

int main() try {
    std::printf("== production\n");
    {
        auto app = keel::compose<Service, SelectedStorage, Network, Compression, Clock>();
        app.init();
        app.getComponent<Service>().handle();
        std::printf("  root sees %s storage\n", app.getComponent<Storage>().describe().c_str());  // by interface
    }

    std::printf("== test: Service with fakes, nothing else\n");
    {
        // Without FakeHealth this doesn't compile: Service needs at least one HealthCheck.
        auto app = keel::compose<Service, FakeStorage, FakeHealth, FakeClock>();
        app.init();
        app.getComponent<Service>().handle();
        std::printf("  fake recorded: %s\n", app.getComponent<FakeStorage>().lastKey.c_str());
    }

    // keel::compose<Service, DiskStorage, MemoryStorage, Clock>();
    //   compile error: AmbiguousDependency<Service, Storage, TypeList<DiskStorage, MemoryStorage>>
} catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
}
