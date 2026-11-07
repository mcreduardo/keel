#include <keel.hpp>

#include "check.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::vector<std::string> events;

class Config : public keel::Component<> {
public:
    Config() = default;
    explicit Config(std::string name) : name_(std::move(name)) {}
    const std::string& name() const { return name_; }

private:
    void init() override { events.push_back("init Config"); }
    void deinit() noexcept override { events.push_back("deinit Config"); }
    std::string name_ = "default";
};

class Logger : public keel::Component<Config> {
public:
    const std::string& prefix() const { return prefix_; }

private:
    void init() override {
        prefix_ = "[" + getComponent<Config>().name() + "]";
        events.push_back("init Logger");
    }
    void deinit() noexcept override { events.push_back("deinit Logger"); }
    std::string prefix_;
};

class Server : public keel::Component<Config, Logger> {
public:
    const Logger* logger() const { return logger_; }

private:
    void init() override {
        logger_ = &getComponent<Logger>();
        events.push_back("init Server");
    }
    void deinit() noexcept override { events.push_back("deinit Server"); }
    const Logger* logger_ = nullptr;
};

struct BrokenDevice {};

class Device : public keel::Component<Logger> {
    void init() override { throw BrokenDevice{}; }
    void deinit() noexcept override { events.push_back("deinit Device"); }
};

}  // namespace

TEST(init_runs_in_list_order_and_deinit_in_reverse) {
    events.clear();
    auto app = keel::compose<Config, Logger, Server>();
    app.init();
    app.deinit();
    CHECK((events == std::vector<std::string>{"init Config", "init Logger", "init Server", "deinit Server",
                                              "deinit Logger", "deinit Config"}));
}

TEST(dependencies_are_reachable_from_init) {
    events.clear();
    auto app = keel::compose<Config, Logger, Server>(std::string("telemetryd"));
    app.init();
    CHECK(app.getComponent<Server>().logger() == &app.getComponent<Logger>());
    CHECK(app.getComponent<Logger>().prefix() == "[telemetryd]");
}

TEST(arguments_go_to_the_components_that_take_them) {
    auto app = keel::compose<Config, Logger>(std::string("edge"));
    CHECK(app.getComponent<Config>().name() == "edge");  // Logger was default-constructed
}

TEST(the_destructor_deinitializes) {
    events.clear();
    {
        auto app = keel::compose<Config, Logger>();
        app.init();
    }
    CHECK((events == std::vector<std::string>{"init Config", "init Logger", "deinit Logger", "deinit Config"}));
}

TEST(a_throwing_init_rolls_back_and_rethrows_unchanged) {
    events.clear();
    auto app = keel::compose<Config, Logger, Device, Server>();
    bool caught = false;
    try {
        app.init();
    } catch (const BrokenDevice&) {
        caught = true;
    }
    CHECK(caught);
    // Device and Server never initialized: only Logger and Config are undone.
    CHECK((events == std::vector<std::string>{"init Config", "init Logger", "deinit Logger", "deinit Config"}));
}

TEST(the_order_comes_from_the_dependencies_not_the_list) {
    events.clear();
    auto app = keel::compose<Server, Logger, Config>();  // backwards
    app.init();
    app.deinit();
    CHECK((events == std::vector<std::string>{"init Config", "init Logger", "init Server", "deinit Server",
                                              "deinit Logger", "deinit Config"}));
}

namespace {
class Metrics : public keel::Component<> {
    void init() override { events.push_back("init Metrics"); }
};
class Tracing : public keel::Component<> {
    void init() override { events.push_back("init Tracing"); }
};
}  // namespace

TEST(independent_components_keep_the_list_order) {
    events.clear();
    auto app = keel::compose<Tracing, Server, Metrics, Logger, Config>();
    app.init();
    CHECK((events == std::vector<std::string>{"init Tracing", "init Metrics", "init Config", "init Logger",
                                              "init Server"}));
}

namespace {
std::vector<std::string> constructed;
struct Tracked {
    explicit Tracked(const char* name) : name_(name) { constructed.push_back(std::string("+") + name_); }
    ~Tracked() { constructed.push_back(std::string("-") + name_); }
    const char* name_;
};
class First : public keel::Component<> {
    Tracked tracked_{"First"};
};
class Second : public keel::Component<First> {
    Tracked tracked_{"Second"};
};
}  // namespace

TEST(construction_follows_the_dependencies_and_destruction_reverses_it) {
    constructed.clear();
    { auto app = keel::compose<Second, First>(); }
    CHECK((constructed == std::vector<std::string>{"+First", "+Second", "-Second", "-First"}));
}

TEST(init_twice_is_an_error) {
    auto app = keel::compose<Config>();
    app.init();
    CHECK_THROWS(app.init());
}

TEST(components_know_their_names) {
    auto app = keel::compose<Config, Logger>();
    CHECK(app.getComponent<Logger>().componentName().find("Logger") != std::string_view::npos);
}

namespace {
class TooEarly : public keel::Component<Config> {
public:
    TooEarly() { getComponent<Config>(); }  // dependencies don't exist yet
};
}  // namespace

TEST(get_component_from_a_constructor_is_an_error) {
    CHECK_THROWS((keel::compose<Config, TooEarly>()));
}
