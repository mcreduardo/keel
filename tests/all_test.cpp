#include <keel.hpp>

#include "check.hpp"

#include <string>
#include <vector>

namespace {

std::vector<std::string> events;

struct HealthCheck {
    virtual ~HealthCheck() = default;
    virtual std::string name() const = 0;
};

struct Plugin {
    virtual ~Plugin() = default;
};

// HealthCheck is the second interface base, so a HealthCheck* to it differs from a pointer
// to the component: getAll must return the subobjects.
struct Labelled {
    virtual ~Labelled() = default;
    int label = 0;
};

class Database : public keel::Component<keel::Provides<HealthCheck>>, public Labelled, public HealthCheck {
public:
    Database() { events.push_back("+Database"); }
    ~Database() override { events.push_back("-Database"); }
    std::string name() const override { return "database"; }

private:
    void init() override { events.push_back("init Database"); }
    void deinit() noexcept override { events.push_back("deinit Database"); }
};

// Depends on Database, so it comes after it whatever the compose<...> order.
class Cache : public keel::Component<Database, keel::Provides<HealthCheck>>, public HealthCheck {
public:
    std::string name() const override { return "cache"; }

private:
    void init() override { events.push_back("init Cache"); }
    void deinit() noexcept override { events.push_back("deinit Cache"); }
};

class Queue : public keel::Component<keel::Provides<HealthCheck>>, public HealthCheck {
public:
    std::string name() const override { return "queue"; }

private:
    void init() override { events.push_back("init Queue"); }
};

std::vector<std::string> namesOf(const std::vector<HealthCheck*>& checks) {
    std::vector<std::string> names;
    for (const HealthCheck* check : checks) names.push_back(check->name());
    return names;
}

class HealthEndpoint : public keel::Component<keel::All<HealthCheck>> {
public:
    HealthEndpoint() { events.push_back("+HealthEndpoint"); }
    ~HealthEndpoint() override { events.push_back("-HealthEndpoint"); }

    const std::vector<HealthCheck*>& checks() const { return checks_; }
    std::vector<HealthCheck*> checksNow() const { return getAll<HealthCheck>(); }

private:
    void init() override {
        checks_ = getAll<HealthCheck>();
        events.push_back("init HealthEndpoint");
    }
    void deinit() noexcept override {
        // Still usable: the providers are deinitialized after this component.
        events.push_back("deinit HealthEndpoint, checks " + std::to_string(getAll<HealthCheck>().size()));
    }
    std::vector<HealthCheck*> checks_;
};

class PluginMenu : public keel::Component<keel::Optional<keel::All<Plugin>>> {
public:
    std::size_t entries() const { return entries_; }

private:
    void init() override { entries_ = getAll<Plugin>().size(); }
    std::size_t entries_ = 99;
};

class Spellcheck : public keel::Component<keel::Provides<Plugin>>, public Plugin {};
class Formatter : public keel::Component<keel::Provides<Plugin>>, public Plugin {};

std::string errorOf(const HealthEndpoint& endpoint) {
    try {
        endpoint.checksNow();
    } catch (const keel::Error& e) {
        return e.what();
    }
    return "no error";
}

}  // namespace

TEST(get_all_returns_every_provider_in_dependency_order) {
    // Listed: Cache before Database, but Cache depends on Database.
    auto app = keel::compose<HealthEndpoint, Queue, Cache, Database>();
    app.init();
    CHECK((namesOf(app.getComponent<HealthEndpoint>().checks()) == std::vector<std::string>{"queue", "database",
                                                                                              "cache"}));
}

TEST(get_all_returns_the_interface_subobjects) {
    auto app = keel::compose<HealthEndpoint, Database>();
    app.init();
    const std::vector<HealthCheck*>& checks = app.getComponent<HealthEndpoint>().checks();
    Database& database = app.getComponent<Database>();
    CHECK(checks.size() == 1);
    CHECK(checks[0] == static_cast<HealthCheck*>(&database));
    CHECK(static_cast<void*>(checks[0]) != static_cast<void*>(&database));
}

TEST(every_provider_comes_before_the_component) {
    events.clear();
    {
        auto app = keel::compose<HealthEndpoint, Cache, Database>();
        app.init();
        app.deinit();
    }
    CHECK((events == std::vector<std::string>{"+Database", "+HealthEndpoint", "init Database", "init Cache",
                                              "init HealthEndpoint", "deinit HealthEndpoint, checks 2",
                                              "deinit Cache", "deinit Database", "-HealthEndpoint", "-Database"}));
}

TEST(an_optional_all_may_be_empty) {
    {
        auto app = keel::compose<PluginMenu>();
        app.init();
        CHECK(app.getComponent<PluginMenu>().entries() == 0);
    }
    {
        auto app = keel::compose<Formatter, PluginMenu, Spellcheck>();
        app.init();
        CHECK(app.getComponent<PluginMenu>().entries() == 2);
    }
}

TEST(get_all_checks_the_lifecycle) {
    auto app = keel::compose<HealthEndpoint, Queue>();
    const HealthEndpoint& endpoint = app.getComponent<HealthEndpoint>();
    CHECK(errorOf(endpoint).find("HealthCheck before init()") != std::string::npos);
    app.init();
    CHECK(endpoint.checksNow().size() == 1);
    app.deinit();
    CHECK(errorOf(endpoint).find("HealthCheck after its deinit()") != std::string::npos);
}

namespace {

class EagerEndpoint : public keel::Component<keel::All<HealthCheck>> {
public:
    EagerEndpoint() {
        try {
            getAll<HealthCheck>();
        } catch (const keel::Error& e) {
            error = e.what();
        }
    }
    std::string error;
};

}  // namespace

TEST(get_all_from_a_constructor_throws) {
    auto app = keel::compose<EagerEndpoint, Queue>();
    CHECK(app.getComponent<EagerEndpoint>().error.find("keel: getAll<") == 0);
    CHECK(app.getComponent<EagerEndpoint>().error.find("from init() on") != std::string::npos);
}

// ---------------------------------------------------------------------------
// A composite: a provider of HealthCheck that uses every other one.

namespace {

class AllChecks : public keel::Component<keel::Provides<HealthCheck>, keel::All<HealthCheck>>, public HealthCheck {
public:
    std::string name() const override {
        std::string out = "all of";
        for (const std::string& n : names_) out += " " + n;
        return out;
    }

private:
    void init() override {
        names_ = namesOf(getAll<HealthCheck>());
        events.push_back("init AllChecks");
    }
    std::vector<std::string> names_;
};

}  // namespace

TEST(a_provider_of_i_with_all_i_gets_the_other_providers) {
    events.clear();
    auto app = keel::compose<AllChecks, Queue, Database>();
    app.init();
    CHECK((events == std::vector<std::string>{"+Database", "init Queue", "init Database", "init AllChecks"}));
    CHECK(app.getComponent<AllChecks>().name() == "all of queue database");
}

TEST(an_all_of_i_includes_a_composite_provider_last) {
    auto app = keel::compose<HealthEndpoint, AllChecks, Queue, Database>();
    app.init();
    CHECK((namesOf(app.getComponent<HealthEndpoint>().checks()) ==
           std::vector<std::string>{"queue", "database", "all of queue database"}));
}

// ---------------------------------------------------------------------------
// All<T> where T is a component: T itself and the subclasses that inherit its Provides<T>.

namespace {

class Clock : public keel::Component<keel::Provides<Clock>> {
public:
    virtual int now() const { return 1000; }
};

class FakeClock : public Clock {
public:
    int now() const override { return 1; }
};

class ClockSum : public keel::Component<keel::All<Clock>> {
public:
    int sum() const {
        int total = 0;
        for (const Clock* clock : getAll<Clock>()) total += clock->now();
        return total;
    }
};

}  // namespace

TEST(all_of_a_component_includes_its_subclasses_that_provide_it) {
    auto app = keel::compose<ClockSum, FakeClock, Clock>();
    app.init();
    CHECK(app.getComponent<ClockSum>().sum() == 1001);
}

namespace {

class PlainClock : public keel::Component<> {
public:
    int now() const { return 5; }
};

class PlainClockSum : public keel::Component<keel::All<PlainClock>> {
public:
    std::vector<PlainClock*> clocks() const { return getAll<PlainClock>(); }
};

}  // namespace

TEST(all_of_a_plain_component_returns_it) {
    auto app = keel::compose<PlainClockSum, PlainClock>();
    app.init();
    const std::vector<PlainClock*> clocks = app.getComponent<PlainClockSum>().clocks();
    CHECK(clocks.size() == 1);
    CHECK(clocks[0] == &app.getComponent<PlainClock>());
}

namespace {

class Watchdog : public keel::Component<keel::All<HealthCheck>> {
    void start() override {
        events.push_back("start Watchdog, checks " + std::to_string(getAll<HealthCheck>().size()));
    }
    void stop() noexcept override {
        events.push_back("stop Watchdog, checks " + std::to_string(getAll<HealthCheck>().size()));
    }
};

}  // namespace

TEST(get_all_works_from_start_and_stop) {
    events.clear();
    {
        auto app = keel::compose<Watchdog, Queue, Database>();
        app.run([] {});
    }
    CHECK((events == std::vector<std::string>{"+Database", "init Queue", "init Database", "start Watchdog, checks 2",
                                              "stop Watchdog, checks 2", "deinit Database", "-Database"}));
}
