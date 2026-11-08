#include <keel.hpp>

#include "check.hpp"

#include <string>
#include <type_traits>
#include <vector>

namespace {

std::vector<std::string> events;

struct Shell {
    virtual ~Shell() = default;
    virtual int serve() = 0;
};

class Config : public keel::Component<> {
public:
    Config() = default;
    explicit Config(int port) : port(port) {}
    int port = 80;

private:
    void init() override { events.push_back("init Config"); }
};

class Logger : public keel::Component<Config> {
    void init() override { events.push_back("init Logger"); }
};

class Router : public keel::Component<Logger> {
    void init() override { events.push_back("init Router"); }
};

class HttpServer : public keel::Component<Config, Router>, public keel::Provides<Shell>, public Shell {
public:
    int serve() override {
        events.push_back("serve");
        return getComponent<Config>().port;
    }

private:
    void init() override { events.push_back("init HttpServer"); }
};

class Metrics : public keel::Component<Logger> {
    void init() override { events.push_back("init Metrics"); }
};

// Listed against the dependency order on purpose: the order comes from the dependencies.
using Core = keel::Bundle<Logger, Config>;
using Http = keel::Bundle<HttpServer, Router>;
using Everything = keel::Bundle<Http, Core>;  // bundles nest
using Nothing = keel::Bundle<>;

using WebApp = keel::App<Http, Core>;

}  // namespace

TEST(a_bundle_stands_for_its_components) {
    events.clear();
    auto app = keel::compose<Http, Core>();
    app.init();
    CHECK(events == (std::vector<std::string>{"init Config", "init Logger", "init Router", "init HttpServer"}));
}

TEST(bundles_nest_and_mix_with_components) {
    events.clear();
    auto app = keel::compose<Metrics, Everything, Nothing>();
    app.init();
    CHECK(events == (std::vector<std::string>{"init Config", "init Logger", "init Metrics", "init Router",
                                              "init HttpServer"}));
}

namespace {
class First : public keel::Component<> {
    void init() override { events.push_back("init First"); }
};
class Second : public keel::Component<> {
    void init() override { events.push_back("init Second"); }
};
}  // namespace

TEST(a_bundle_is_expanded_in_place) {
    // First and Second don't depend on each other, so they keep the written order.
    events.clear();
    {
        auto app = keel::compose<keel::Bundle<First, Second>>();
        app.init();
    }
    CHECK(events == (std::vector<std::string>{"init First", "init Second"}));
    events.clear();
    {
        auto app = keel::compose<Second, keel::Bundle<keel::Bundle<First>>>();
        app.init();
    }
    CHECK(events == (std::vector<std::string>{"init Second", "init First"}));
}

TEST(a_bundle_gives_the_same_order_as_its_components) {
    events.clear();
    {
        auto app = keel::compose<HttpServer, Router, Logger, Config>();
        app.init();
    }
    const std::vector<std::string> flat = events;
    events.clear();
    {
        auto app = keel::compose<Everything>();
        app.init();
    }
    CHECK(events == flat);
}

TEST(an_app_of_bundles_gets_components_and_interfaces) {
    WebApp app(8080);  // arguments reach the components inside bundles
    CHECK(app.getComponent<Config>().port == 8080);
    app.init();
    CHECK(&app.getComponent<Shell>() == static_cast<Shell*>(&app.getComponent<HttpServer>()));
}

TEST(an_app_of_bundles_runs) {
    events.clear();
    WebApp app;
    CHECK(app.run(&HttpServer::serve) == 80);
    CHECK(events.back() == "serve");

    WebApp second;
    CHECK(second.run(&Shell::serve) == 80);

    WebApp third;
    CHECK(third.run([](WebApp& running) { return running.getComponent<Config>().port; }) == 80);

    CHECK(keel::run<WebApp>(&HttpServer::serve, 9090) == 9090);
    CHECK(keel::run<keel::App<Everything>>([] { return 7; }) == 7);
}

TEST(compose_returns_the_app_of_its_arguments) {
    CHECK((std::is_same_v<decltype(keel::compose<Http, Core>()), WebApp>));
}

// A component in two bundles is listed twice (DuplicateComponent<Logger>), and a bundle
// reached twice is DuplicateBundle<Core>.
static_assert(keel::isValid<Core, Http>);
static_assert(keel::isValid<Everything>);
static_assert(keel::isValid<Everything, Nothing, Metrics>);
static_assert(!keel::isValid<Everything, Core>);
static_assert(!keel::isValid<Core, keel::Bundle<Logger>>);
static_assert(!keel::isValid<Http>);  // Router needs Logger, HttpServer needs Config
static_assert(!keel::isValid<Nothing>);
