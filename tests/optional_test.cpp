#include <keel.hpp>

#include "check.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::string> events;

class Metrics : public keel::Component<> {
public:
    Metrics() { events.push_back("+Metrics"); }
    ~Metrics() override { events.push_back("-Metrics"); }

private:
    void init() override { events.push_back("init Metrics"); }
    void deinit() noexcept override { events.push_back("deinit Metrics"); }
};

class Router : public keel::Component<> {
    void init() override { events.push_back("init Router"); }
};

class HttpServer : public keel::Component<Router, keel::Optional<Metrics>> {
public:
    HttpServer() { events.push_back("+HttpServer"); }
    ~HttpServer() override { events.push_back("-HttpServer"); }

    Metrics* metrics() const { return metrics_; }
    Metrics* findNow() const { return findComponent<Metrics>(); }

private:
    void init() override {
        metrics_ = findComponent<Metrics>();
        events.push_back("init HttpServer");
    }
    void deinit() noexcept override {
        // Still reachable here: Metrics is deinitialized after this component.
        events.push_back(findComponent<Metrics>() != nullptr ? "deinit HttpServer, Metrics found"
                                                              : "deinit HttpServer");
    }
    Metrics* metrics_ = nullptr;
};

std::string errorOf(const HttpServer& server) {
    try {
        server.findNow();
    } catch (const keel::Error& e) {
        return e.what();
    }
    return "no error";
}

}  // namespace

TEST(an_absent_optional_dependency_is_nullptr) {
    events.clear();
    auto app = keel::compose<Router, HttpServer>();
    app.init();
    app.deinit();
    CHECK(app.getComponent<HttpServer>().metrics() == nullptr);
    CHECK((events == std::vector<std::string>{"+HttpServer", "init Router", "init HttpServer", "deinit HttpServer"}));
}

TEST(a_composed_optional_dependency_is_found_and_initialized_first) {
    events.clear();
    {
        auto app = keel::compose<HttpServer, Router, Metrics>();  // Metrics listed last
        app.init();
        CHECK(app.getComponent<HttpServer>().metrics() == &app.getComponent<Metrics>());
        app.deinit();
    }
    // Construction, init, deinit and destruction all follow the optional dependency.
    CHECK((events == std::vector<std::string>{"+Metrics", "+HttpServer", "init Router", "init Metrics",
                                              "init HttpServer", "deinit HttpServer, Metrics found",
                                              "deinit Metrics", "-HttpServer", "-Metrics"}));
}

// A lookup at the wrong time is an error whether or not the optional dependency is composed.
TEST(find_component_before_init_or_after_deinit_is_an_error_even_if_absent) {
    auto absent = keel::compose<Router, HttpServer>();
    CHECK(errorOf(absent.getComponent<HttpServer>()).find("before init()") != std::string::npos);
    absent.init();
    CHECK(absent.getComponent<HttpServer>().findNow() == nullptr);
    absent.deinit();
    CHECK(errorOf(absent.getComponent<HttpServer>()).find("after its deinit()") != std::string::npos);

    auto present = keel::compose<Router, Metrics, HttpServer>();
    CHECK(errorOf(present.getComponent<HttpServer>()).find("before init()") != std::string::npos);
    present.init();
    CHECK(present.getComponent<HttpServer>().findNow() == &present.getComponent<Metrics>());
    present.deinit();
    CHECK(errorOf(present.getComponent<HttpServer>()).find("after its deinit()") != std::string::npos);
}

namespace {
class EagerServer : public keel::Component<keel::Optional<Metrics>> {
public:
    EagerServer() { findComponent<Metrics>(); }  // dependencies don't exist yet
};

std::string constructionError() {
    try {
        auto app = keel::compose<EagerServer>();
    } catch (const keel::Error& e) {
        return e.what();
    }
    return "no error";
}
}  // namespace

TEST(find_component_from_a_constructor_is_an_error) {
    CHECK(std::string_view(constructionError()).starts_with("keel: findComponent<"));
}

namespace {
class Never;  // declared, never defined, never composed
class Reporter : public keel::Component<keel::Optional<Never>> {
public:
    bool found() const { return findComponent<Never>() != nullptr; }
};
}  // namespace

TEST(an_optional_dependency_may_be_an_incomplete_type) {
    auto app = keel::compose<Reporter>();
    app.init();
    CHECK(!app.getComponent<Reporter>().found());
}
