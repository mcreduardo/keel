#include <keel.hpp>

#include "check.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

std::vector<std::string> trace;

struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Config : public keel::Component<> {
public:
    Config() = default;
    explicit Config(int port) : port(port) {}
    int port = 80;

private:
    void init() override { trace.push_back("init Config"); }
    void start() override { trace.push_back("start Config"); }
    void stop() noexcept override { trace.push_back("stop Config"); }
    void deinit() noexcept override { trace.push_back("deinit Config"); }
};

class Server : public keel::Component<Config> {
public:
    int serve() {
        trace.push_back("serve");
        return getComponent<Config>().port;
    }
    int port() const { return getComponent<Config>().port; }
    int portNoexcept() noexcept { return getComponent<Config>().port; }
    int overloaded() { return 1; }
    int overloaded(int n) { return n; }
    void fail() { throw Failure("serve failed"); }

private:
    void init() override { trace.push_back("init Server"); }
    void start() override { trace.push_back("start Server"); }
    void stop() noexcept override { trace.push_back("stop Server"); }
    void deinit() noexcept override { trace.push_back("deinit Server"); }
};

class BadStart : public keel::Component<Config> {
    void start() override { throw Failure("start BadStart"); }
};

using ServerApp = keel::App<Server, Config>;
using Strings = std::vector<std::string>;
const Strings fullRun{"init Config", "init Server", "start Config", "start Server", "serve",
                      "stop Server", "stop Config", "deinit Server", "deinit Config"};

}  // namespace

TEST(run_calls_a_callable_with_no_arguments_between_start_and_stop) {
    trace.clear();
    auto app = keel::compose<Server, Config>();
    app.run([] { trace.push_back("serve"); });
    CHECK(trace == fullRun);
}

TEST(run_passes_the_app_to_a_callable_that_takes_it) {
    trace.clear();
    ServerApp app;
    int port = app.run([](ServerApp& running) { return running.getComponent<Server>().serve(); });
    CHECK(port == 80);
    CHECK(trace == fullRun);
}

TEST(run_passes_the_app_to_a_generic_lambda) {
    ServerApp app;
    CHECK(app.run([](auto& running) { return std::is_same_v<decltype(running), ServerApp&>; }));
}

TEST(run_prefers_the_app_when_the_callable_also_takes_nothing) {
    ServerApp app;
    CHECK(app.run([](auto&&... args) { return sizeof...(args); }) == 1);
}

TEST(run_calls_a_function_object) {
    struct Entry {
        int operator()(ServerApp& running) const { return running.getComponent<Config>().port; }
    };
    ServerApp app(8080);
    CHECK(app.run(Entry{}) == 8080);
}

TEST(run_calls_a_member_function_on_its_component) {
    trace.clear();
    auto app = keel::compose<Server, Config>(8080);
    CHECK(app.run(&Server::serve) == 8080);
    CHECK(trace == fullRun);
}

TEST(run_calls_a_const_member_function) {
    auto app = keel::compose<Server, Config>(8080);
    CHECK(app.run(&Server::port) == 8080);
}

TEST(run_calls_a_noexcept_member_function) {
    auto app = keel::compose<Server, Config>(8080);
    CHECK(app.run(&Server::portNoexcept) == 8080);
}

TEST(run_picks_the_overload_without_arguments) {
    ServerApp app;
    CHECK(app.run(&Server::overloaded) == 1);
    CHECK(keel::run<ServerApp>(&Server::overloaded) == 1);
}

TEST(run_shuts_down_after_the_callable_stopped_the_app) {
    trace.clear();
    ServerApp app;
    app.run([](ServerApp& running) {
        running.stop();
        trace.push_back("stopped");
    });
    CHECK((trace == Strings{"init Config", "init Server", "start Config", "start Server", "stop Server",
                            "stop Config", "stopped", "deinit Server", "deinit Config"}));
}

TEST(run_returns_what_the_callable_returns) {
    auto app = keel::compose<Config>();
    int value = 7;
    static_assert(std::is_same_v<decltype(app.run([&value]() -> int& { return value; })), int&>);
    CHECK(&app.run([&value]() -> int& { return value; }) == &value);
}

TEST(run_shuts_down_when_the_callable_throws) {
    trace.clear();
    auto app = keel::compose<Server, Config>();
    std::string error;
    try {
        app.run(&Server::fail);
    } catch (const Failure& e) {
        error = e.what();
    }
    CHECK(error == "serve failed");
    CHECK((trace == Strings{"init Config", "init Server", "start Config", "start Server", "stop Server",
                            "stop Config", "deinit Server", "deinit Config"}));
}

TEST(run_does_not_call_the_callable_when_start_fails) {
    trace.clear();
    auto app = keel::compose<Config, BadStart>();
    bool called = false;
    std::string error;
    try {
        app.run([&called] { called = true; });
    } catch (const Failure& e) {
        error = e.what();
    }
    CHECK(!called);
    CHECK(error == "start BadStart");
    CHECK((trace == Strings{"init Config", "start Config", "stop Config", "deinit Config"}));
}

TEST(run_after_init_is_an_error) {
    auto app = keel::compose<Config>();
    app.init();
    CHECK_THROWS(app.run([] {}));
}

TEST(keel_run_constructs_the_app_and_runs_it) {
    trace.clear();
    CHECK(keel::run<ServerApp>(&Server::serve, 9090) == 9090);
    CHECK(trace == fullRun);
}

TEST(keel_run_returns_by_value) {
    // The app is destroyed when keel::run returns, so a reference result is copied out.
    using ConfigApp = keel::App<Config>;
    int value = 7;
    static_assert(std::is_same_v<decltype(keel::run<ConfigApp>([&value]() -> int& { return value; })), int>);
    CHECK(keel::run<ConfigApp>([&value]() -> int& { return value; }) == 7);
}

TEST(keel_run_runs_a_void_callable) {
    trace.clear();
    keel::run<ServerApp>([] { trace.push_back("serve"); });
    CHECK(trace == fullRun);
}

TEST(keel_run_returns_move_only_results) {
    auto p = keel::run<keel::App<Config>>([] { return std::make_unique<int>(3); });
    CHECK(*p == 3);
}
