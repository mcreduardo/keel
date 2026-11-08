#include <keel.hpp>

#include "check.hpp"

#include <string>
#include <string_view>
#include <vector>

// The types are in a named namespace, not an anonymous one, so that the expected error
// messages can spell their names exactly ("decisions::Cache"): the anonymous namespace is
// printed differently by each compiler.
namespace decisions {

namespace {

std::vector<std::string> events;

// What the configuration says; set by each test before init().
struct Settings {
    bool telemetry = false;
    std::string cache = "memory";
};
Settings settings;

bool contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

template <class App>
std::string initError(App& app) {
    try {
        app.init();
    } catch (const keel::Error& e) {
        return e.what();
    }
    return "no error";
}

template <class Fn>
std::string errorOf(Fn fn) {
    try {
        fn();
    } catch (const keel::Error& e) {
        return e.what();
    }
    return "no error";
}

}  // namespace

class Config : public keel::Component<> {
public:
    const Settings& get() const { return settings; }

private:
    void init() override { events.push_back("init Config"); }
};

// A component that logs every hook, and that configuration turns on or off.
class Telemetry : public keel::Component<Config>, public keel::Conditional {
public:
    Telemetry() { events.push_back("+Telemetry"); }
    ~Telemetry() override { events.push_back("-Telemetry"); }

    // Called by the tests on a disabled Telemetry.
    const Config& config() const { return getComponent<Config>(); }

private:
    bool enabled() const override {
        events.push_back("enabled Telemetry");
        return getComponent<Config>().get().telemetry;  // a dependency, already initialized
    }
    void init() override { events.push_back("init Telemetry"); }
    void start() override { events.push_back("start Telemetry"); }
    void stop() noexcept override { events.push_back("stop Telemetry"); }
    void deinit() noexcept override { events.push_back("deinit Telemetry"); }
};

class Server : public keel::Component<keel::Optional<Telemetry>> {
public:
    Telemetry* telemetry() const { return telemetry_; }

private:
    void init() override {
        telemetry_ = findComponent<Telemetry>();
        events.push_back("init Server");
    }
    void start() override { events.push_back("start Server"); }
    void deinit() noexcept override { events.push_back("deinit Server"); }
    Telemetry* telemetry_ = nullptr;
};

// Nothing may require a conditional component, a conditional one included.
class Exporter : public keel::Component<Telemetry> {};
class Mirror : public keel::Component<Telemetry>, public keel::Conditional {
    bool enabled() const override { return true; }
};

// A conditional component may use another one through keel::Optional<>.
class Uplink : public keel::Component<keel::Optional<Telemetry>>, public keel::Conditional {
public:
    Telemetry* telemetry() const { return telemetry_; }

private:
    bool enabled() const override {
        events.push_back("enabled Uplink");
        return true;
    }
    void init() override {
        telemetry_ = findComponent<Telemetry>();
        events.push_back("init Uplink");
    }
    Telemetry* telemetry_ = nullptr;
};

// enabled() may throw, like init(): the components already initialized are rolled back.
class Broken : public keel::Component<Config>, public keel::Conditional {
    bool enabled() const override { throw keel::Error("no configuration for Broken"); }
    void deinit() noexcept override { events.push_back("deinit Broken"); }
};
class Logger : public keel::Component<> {
    void init() override { events.push_back("init Logger"); }
    void deinit() noexcept override { events.push_back("deinit Logger"); }
};

// Not a keel::Conditional: its enabled() is its own, and keel never asks it.
class Lamp : public keel::Component<> {
public:
    bool enabled() const { return false; }

private:
    void init() override { events.push_back("init Lamp"); }
    void start() override { events.push_back("start Lamp"); }
    void stop() noexcept override { events.push_back("stop Lamp"); }
    void deinit() noexcept override { events.push_back("deinit Lamp"); }
};
class Room : public keel::Component<Lamp> {};  // may require it

// An interface whose one provider is conditional.
struct Sink {
    virtual ~Sink() = default;
};
class FileSink : public keel::Component<>, public keel::Provides<Sink>, public Sink, public keel::Conditional {
    bool enabled() const override { return false; }
};
class Writer : public keel::Component<Sink> {};
class MaybeWriter : public keel::Component<keel::Optional<Sink>> {};

}  // namespace decisions

using namespace decisions;

// Required dependencies on conditional components don't compile (see the compile_fail
// tests for the errors).
static_assert(!keel::isValid<Exporter, Telemetry, Config>);
static_assert(!keel::isValid<Mirror, Telemetry, Config>);
static_assert(!keel::isValid<Writer, FileSink>);
static_assert(keel::isValid<MaybeWriter, FileSink>);
static_assert(keel::isValid<Room, Lamp>);

TEST(a_disabled_component_runs_no_hooks_and_is_nullptr_for_dependents) {
    events.clear();
    settings = {};
    {
        auto app = keel::compose<Server, Telemetry, Config>();
        app.init();
        app.start();
        CHECK(app.getComponent<Server>().telemetry() == nullptr);
        app.stop();
        app.deinit();
    }
    // Constructed and destroyed, asked after Config is initialized, nothing else.
    CHECK((events == std::vector<std::string>{"+Telemetry", "init Config", "enabled Telemetry", "init Server",
                                              "start Server", "deinit Server", "-Telemetry"}));
}

TEST(an_enabled_component_is_asked_once_before_its_init) {
    events.clear();
    settings = {};
    settings.telemetry = true;
    {
        auto app = keel::compose<Server, Telemetry, Config>();
        app.init();
        app.start();
        CHECK(app.getComponent<Server>().telemetry() == &app.getComponent<Telemetry>());
    }
    CHECK((events == std::vector<std::string>{"+Telemetry", "init Config", "enabled Telemetry", "init Telemetry",
                                              "init Server", "start Telemetry", "start Server", "stop Telemetry",
                                              "deinit Server", "deinit Telemetry", "-Telemetry"}));
}

TEST(a_conditional_component_uses_another_through_optional) {
    events.clear();
    settings = {};
    auto app = keel::compose<Uplink, Telemetry, Config>();
    app.init();
    CHECK(app.getComponent<Uplink>().telemetry() == nullptr);
    CHECK((events == std::vector<std::string>{"+Telemetry", "init Config", "enabled Telemetry", "enabled Uplink",
                                              "init Uplink"}));
}

TEST(a_component_that_isnt_conditional_runs_every_hook_whatever_its_enabled_says) {
    events.clear();
    {
        auto app = keel::compose<Room, Lamp>();
        app.init();
        app.start();
        CHECK(!app.getComponent<Lamp>().enabled());
    }
    CHECK((events == std::vector<std::string>{"init Lamp", "start Lamp", "stop Lamp", "deinit Lamp"}));
}

TEST(the_root_gets_a_disabled_component_only_before_init) {
    settings = {};
    auto app = keel::compose<Server, Telemetry, Config>();
    Telemetry& telemetry = app.getComponent<Telemetry>();  // before init(): not decided yet
    app.init();
    CHECK(errorOf([&] { app.getComponent<Telemetry>(); }) ==
          "keel: getComponent<decisions::Telemetry>(): decisions::Telemetry is disabled (its enabled() returned "
          "false)");
    // A disabled component can't use its dependencies.
    CHECK(errorOf([&] { telemetry.config(); }) ==
          "keel: decisions::Telemetry used decisions::Config, but decisions::Telemetry is disabled (its enabled() "
          "returned false)");
}

TEST(the_root_names_the_disabled_provider_of_an_interface) {
    auto app = keel::compose<MaybeWriter, FileSink>();
    app.init();
    CHECK(errorOf([&] { app.getComponent<Sink>(); }) ==
          "keel: getComponent<decisions::Sink>(): its provider decisions::FileSink is disabled (its enabled() "
          "returned false)");
}

TEST(a_throwing_enabled_rolls_back_like_init) {
    events.clear();
    auto app = keel::compose<Logger, Broken, Config>();
    CHECK(initError(app) == "no configuration for Broken");
    CHECK((events == std::vector<std::string>{"init Logger", "init Config", "deinit Logger"}));
}

// ---------------------------------------------------------------------------
// Collections leave disabled providers out.

namespace decisions {

struct HealthCheck {
    virtual ~HealthCheck() = default;
    virtual std::string name() const = 0;
};

template <bool on, int id = 0>
class Check : public keel::Component<>, public keel::Provides<HealthCheck>, public HealthCheck,
              public keel::Conditional {
public:
    std::string name() const override { return on ? "on" : "off"; }

private:
    bool enabled() const override { return on; }
};

class Always : public keel::Component<>, public keel::Provides<HealthCheck>, public HealthCheck {
public:
    std::string name() const override { return "always"; }
};

class Health : public keel::Component<keel::All<HealthCheck>> {
public:
    std::vector<std::string> names() const { return names_; }

private:
    void init() override {
        for (const HealthCheck* check : getAll<HealthCheck>()) names_.push_back(check->name());
    }
    std::vector<std::string> names_;
};

class Plugins : public keel::Component<keel::Optional<keel::All<HealthCheck>>> {
public:
    std::size_t count() const { return count_; }

private:
    void init() override { count_ = getAll<HealthCheck>().size(); }
    std::size_t count_ = 99;
};

}  // namespace decisions

// keel::All<I> needs a provider that is always there.
static_assert(!keel::isValid<Health, Check<true>>);
static_assert(keel::isValid<Health, Check<true>, Always>);

TEST(get_all_leaves_disabled_providers_out) {
    auto app = keel::compose<Health, Check<false>, Check<true>, Check<false, 1>, Always>();
    app.init();
    CHECK((app.getComponent<Health>().names() == std::vector<std::string>{"on", "always"}));
}

TEST(optional_all_with_every_provider_disabled_is_empty) {
    auto app = keel::compose<Plugins, Check<false>, Check<false, 1>>();
    app.init();
    CHECK(app.getComponent<Plugins>().count() == 0);
}

// ---------------------------------------------------------------------------
// keel::Alternative<I>

namespace decisions {

struct Cache {
    virtual ~Cache() = default;
    virtual std::string kind() const = 0;
};

class RedisCache : public keel::Component<Config>, public keel::Alternative<Cache>, public Cache,
                   public keel::Conditional {
public:
    std::string kind() const override { return "redis"; }

private:
    bool enabled() const override {
        const std::string& cache = getComponent<Config>().get().cache;
        if (cache == "throw") throw keel::Error("bad cache setting");
        return cache == "redis" || cache == "both";
    }
    void init() override { events.push_back("init RedisCache"); }
    void start() override { events.push_back("start RedisCache"); }
};

class MemoryCache : public keel::Component<Config>, public keel::Alternative<Cache>, public Cache,
                    public keel::Conditional {
public:
    std::string kind() const override { return "memory"; }

private:
    bool enabled() const override {
        const std::string& cache = getComponent<Config>().get().cache;
        return cache == "memory" || cache == "both";
    }
    void init() override { events.push_back("init MemoryCache"); }
    void start() override { events.push_back("start MemoryCache"); }
};

class Repository : public keel::Component<Cache> {
public:
    std::string kind() const { return cache_->kind(); }

private:
    void init() override {
        cache_ = &getComponent<Cache>();
        events.push_back("init Repository");
    }
    Cache* cache_ = nullptr;
};

class Report : public keel::Component<keel::Optional<Cache>> {
public:
    Cache* cache() const { return cache_; }

private:
    void init() override { cache_ = findComponent<Cache>(); }
    Cache* cache_ = nullptr;
};

// Lists keel::Optional<Cache> but never looks it up: init() still checks it.
class Unused : public keel::Component<keel::Optional<Cache>> {};

class Warmup : public keel::Component<keel::After<Cache>> {
    void init() override { events.push_back("init Warmup"); }
};

// keel::All<Cache> would be an error: every alternative is conditional.
class Caches : public keel::Component<keel::Optional<keel::All<Cache>>> {
public:
    std::vector<std::string> kinds() const { return kinds_; }

private:
    void init() override {
        for (const Cache* cache : getAll<Cache>()) kinds_.push_back(cache->kind());
    }
    std::vector<std::string> kinds_;
};

// Fails before the alternatives are asked: it is listed first and they don't depend on it.
class Doomed : public keel::Component<> {
    void init() override { throw keel::Error("Doomed failed"); }
};

}  // namespace decisions

TEST(dependents_get_the_enabled_alternative_and_come_after_every_alternative) {
    for (std::string choice : {"redis", "memory"}) {
        const std::string chosen = choice == "redis" ? "init RedisCache" : "init MemoryCache";
        events.clear();
        settings = {};
        settings.cache = choice;
        // Listed first, but each comes after both alternatives.
        auto app = keel::compose<Repository, Warmup, RedisCache, MemoryCache, Config>();
        app.init();
        CHECK(app.getComponent<Repository>().kind() == choice);
        CHECK(app.getComponent<Cache>().kind() == choice);
        CHECK((events == std::vector<std::string>{"init Config", chosen, "init Repository", "init Warmup"}));
    }
}

TEST(no_enabled_alternative_fails_a_required_dependent) {
    settings = {};
    settings.cache = "none";
    auto app = keel::compose<Repository, RedisCache, MemoryCache, Config>();
    CHECK(initError(app) ==
          "keel: decisions::Repository uses decisions::Cache: none of its alternatives (decisions::RedisCache, "
          "decisions::MemoryCache) is enabled");
}

TEST(no_enabled_alternative_is_nullptr_for_an_optional_dependent) {
    settings = {};
    settings.cache = "none";
    auto app = keel::compose<Report, RedisCache, MemoryCache, Config>();
    app.init();
    CHECK(app.getComponent<Report>().cache() == nullptr);
}

TEST(several_enabled_alternatives_fail_init) {
    settings = {};
    settings.cache = "both";
    {
        auto app = keel::compose<Repository, RedisCache, MemoryCache, Config>();
        CHECK(initError(app) ==
              "keel: decisions::Repository uses decisions::Cache: several of its alternatives are enabled (only "
              "one may be): decisions::RedisCache, decisions::MemoryCache");
    }
    {
        // Also for keel::Optional<Cache>, even if the component never looks it up.
        auto app = keel::compose<Unused, RedisCache, MemoryCache, Config>();
        CHECK(contains(initError(app), "decisions::Unused uses decisions::Cache: several of its alternatives"));
    }
}

TEST(optional_all_gets_the_enabled_alternatives) {
    for (const char* choice : {"both", "redis", "none"}) {
        settings = {};
        settings.cache = choice;
        auto app = keel::compose<Caches, RedisCache, MemoryCache, Config>();
        app.init();
        const std::vector<std::string> expected =
            settings.cache == "both"    ? std::vector<std::string>{"redis", "memory"}
            : settings.cache == "redis" ? std::vector<std::string>{"redis"}
                                        : std::vector<std::string>{};
        CHECK(app.getComponent<Caches>().kinds() == expected);
    }
}

TEST(the_root_gets_an_alternative_only_after_init) {
    settings = {};
    settings.cache = "redis";
    auto app = keel::compose<RedisCache, MemoryCache, Config>();
    CHECK(errorOf([&] { app.getComponent<Cache>(); }) ==
          "keel: getComponent<decisions::Cache>(): init() chooses among its alternatives (decisions::RedisCache, "
          "decisions::MemoryCache); call it after init()");
    app.init();
    CHECK(&app.getComponent<Cache>() == static_cast<Cache*>(&app.getComponent<RedisCache>()));
    // The alternatives themselves are components: the disabled one is an error.
    CHECK_THROWS(app.getComponent<MemoryCache>());
}

TEST(the_root_reports_no_enabled_alternative) {
    settings = {};
    settings.cache = "none";
    auto app = keel::compose<RedisCache, Config>();  // a single alternative is chosen at runtime too
    CHECK_THROWS(app.getComponent<Cache>());
    app.init();
    CHECK(errorOf([&] { app.getComponent<Cache>(); }) ==
          "keel: getComponent<decisions::Cache>(): none of its alternatives (decisions::RedisCache) is enabled");
}

TEST(the_root_reports_several_enabled_alternatives) {
    // Nothing depends on Cache, so init() doesn't check the choice; the root's lookup does.
    settings = {};
    settings.cache = "both";
    auto app = keel::compose<RedisCache, MemoryCache, Config>();
    app.init();
    CHECK(errorOf([&] { app.getComponent<Cache>(); }) ==
          "keel: getComponent<decisions::Cache>(): several of its alternatives are enabled (only one may be): "
          "decisions::RedisCache, decisions::MemoryCache");
}

namespace decisions {

// An alternative inherited from a component base class.
class BaseCache : public keel::Component<>, public keel::Alternative<Cache>, public Cache, public keel::Conditional {
public:
    std::string kind() const override { return "base"; }

private:
    bool enabled() const override { return true; }
};
class DerivedCache : public BaseCache {
public:
    std::string kind() const override { return "derived"; }
};

}  // namespace decisions

TEST(an_inherited_alternative_is_found) {
    using namespace decisions;
    auto app = keel::compose<DerivedCache>();
    app.init();
    CHECK(&app.getComponent<Cache>() == static_cast<Cache*>(&app.getComponent<DerivedCache>()));
    CHECK(keel::run<keel::App<DerivedCache>>(&Cache::kind) == "derived");
}

TEST(the_root_gets_no_alternative_after_a_failed_init) {
    const std::string undecided =
        "keel: getComponent<decisions::Cache>(): init() failed before choosing among its alternatives (";
    {
        // MemoryCache is disabled, RedisCache's enabled() throws: neither is chosen.
        settings = {};
        settings.cache = "throw";
        auto app = keel::compose<MemoryCache, RedisCache, Config>();
        CHECK(initError(app) == "bad cache setting");
        CHECK(errorOf([&] { app.getComponent<Cache>(); }) ==
              undecided + "decisions::MemoryCache, decisions::RedisCache)");
    }
    {
        // An earlier component fails before either is asked.
        settings = {};
        auto app = keel::compose<Doomed, RedisCache, MemoryCache, Config>();
        CHECK(initError(app) == "Doomed failed");
        CHECK(errorOf([&] { app.getComponent<Cache>(); }) ==
              undecided + "decisions::RedisCache, decisions::MemoryCache)");
    }
}

TEST(run_calls_the_enabled_alternative) {
    settings = {};
    settings.cache = "memory";
    using CacheApp = keel::App<RedisCache, MemoryCache, Config>;
    CHECK(keel::run<CacheApp>(&Cache::kind) == "memory");
}

TEST(run_without_an_enabled_alternative_throws_before_start) {
    events.clear();
    settings = {};
    settings.cache = "none";
    using CacheApp = keel::App<Server, RedisCache, MemoryCache, Config>;
    CHECK(contains(errorOf([] { keel::run<CacheApp>(&Cache::kind); }), "none of its alternatives"));
    CHECK((events == std::vector<std::string>{"init Server", "init Config", "deinit Server"}));
}
