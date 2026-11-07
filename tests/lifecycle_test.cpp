#include <keel.hpp>

#include "check.hpp"

#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using Strings = std::vector<std::string>;

Strings trace;

struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

enum class Throws { never, inInit, inStart };

template <char Name, Throws throws = Throws::never, class... Deps>
class Part : public keel::Component<Deps...> {
    void init() override {
        trace.push_back(std::string("init ") + Name);
        if (throws == Throws::inInit) throw Failure(std::string("init ") + Name);
    }
    void start() override {
        trace.push_back(std::string("start ") + Name);
        if (throws == Throws::inStart) throw Failure(std::string("start ") + Name);
    }
    void stop() noexcept override { trace.push_back(std::string("stop ") + Name); }
    void deinit() noexcept override { trace.push_back(std::string("deinit ") + Name); }
};

using A = Part<'A'>;
using B = Part<'B', Throws::never, A>;
using BadInitC = Part<'C', Throws::inInit, B>;
using BadStartC = Part<'C', Throws::inStart, B>;
using D = Part<'D', Throws::never, BadStartC>;

}  // namespace

static_assert(!std::is_copy_constructible_v<keel::App<A>>);
static_assert(!std::is_move_constructible_v<keel::App<A>>);
static_assert(!std::is_move_assignable_v<keel::App<A>>);

TEST(a_failed_init_rolls_back_and_keeps_its_exception) {
    trace.clear();
    auto app = keel::compose<A, B, BadInitC>();
    std::string error;
    try {
        app.init();
    } catch (const Failure& e) {
        error = e.what();
    }
    CHECK(error == "init C");
    CHECK((trace == std::vector<std::string>{"init A", "init B", "init C", "deinit B", "deinit A"}));
    app.deinit();  // nothing left to do
    CHECK(trace.size() == 5);
}

TEST(init_after_a_failed_init_is_an_error) {
    auto app = keel::compose<A, B, BadInitC>();
    CHECK_THROWS(app.init());
    CHECK_THROWS(app.init());
}

TEST(deinit_twice_does_nothing_the_second_time) {
    trace.clear();
    auto app = keel::compose<A, B>();
    app.init();
    app.deinit();
    app.deinit();
    CHECK((trace == std::vector<std::string>{"init A", "init B", "deinit B", "deinit A"}));
}

static_assert(noexcept(std::declval<keel::App<A>&>().deinit()));
static_assert(noexcept(std::declval<keel::App<A>&>().stop()));

TEST(start_follows_every_init_and_stop_runs_in_reverse) {
    trace.clear();
    auto app = keel::compose<B, A>();
    app.init();
    app.start();
    app.stop();
    app.deinit();
    CHECK((trace == Strings{"init A", "init B", "start A", "start B", "stop B", "stop A", "deinit B", "deinit A"}));
}

TEST(deinit_stops_a_started_app_first) {
    trace.clear();
    auto app = keel::compose<A, B>();
    app.init();
    app.start();
    app.deinit();
    CHECK((trace == Strings{"init A", "init B", "start A", "start B", "stop B", "stop A", "deinit B", "deinit A"}));
}

TEST(the_destructor_stops_and_deinitializes_a_started_app) {
    trace.clear();
    {
        auto app = keel::compose<A, B>();
        app.init();
        app.start();
    }
    CHECK((trace == Strings{"init A", "init B", "start A", "start B", "stop B", "stop A", "deinit B", "deinit A"}));
}

TEST(stop_twice_does_nothing_the_second_time) {
    trace.clear();
    auto app = keel::compose<A, B>();
    app.init();
    app.start();
    app.stop();
    app.stop();
    CHECK((trace == Strings{"init A", "init B", "start A", "start B", "stop B", "stop A"}));
}

TEST(stop_before_start_does_nothing) {
    trace.clear();
    auto app = keel::compose<A, B>();
    app.stop();
    app.init();
    app.stop();
    CHECK((trace == Strings{"init A", "init B"}));
}

TEST(a_failed_start_stops_and_deinitializes_and_keeps_its_exception) {
    trace.clear();
    auto app = keel::compose<A, B, BadStartC, D>();
    app.init();
    std::string error;
    try {
        app.start();
    } catch (const Failure& e) {
        error = e.what();
    }
    CHECK(error == "start C");
    // C's start() failed, so C isn't stopped, but its init() succeeded, so it is deinitialized.
    CHECK((trace == Strings{"init A", "init B", "init C", "init D", "start A", "start B", "start C", "stop B",
                            "stop A", "deinit D", "deinit C", "deinit B", "deinit A"}));
    app.stop();  // nothing left to do
    app.deinit();
    CHECK(trace.size() == 13);
}

TEST(start_requires_a_successful_init_and_runs_once) {
    {
        auto app = keel::compose<A, B>();
        CHECK_THROWS(app.start());  // before init()
        app.init();
        app.start();
        CHECK_THROWS(app.start());  // twice
        app.stop();
        CHECK_THROWS(app.start());  // after stop()
        CHECK_THROWS(app.init());
    }
    {
        auto app = keel::compose<A, B, BadInitC>();
        CHECK_THROWS(app.init());
        CHECK_THROWS(app.start());  // after a failed init()
    }
    {
        auto app = keel::compose<A, B, BadStartC>();
        app.init();
        CHECK_THROWS(app.start());
        CHECK_THROWS(app.start());  // after a failed start()
        CHECK_THROWS(app.init());
    }
    {
        auto app = keel::compose<A, B>();
        app.init();
        app.deinit();
        CHECK_THROWS(app.start());  // after deinit()
    }
}

namespace {
template <class App>
std::string startError(App& app) {
    try {
        app.start();
    } catch (const keel::Error& e) {
        return e.what();
    }
    return "no keel::Error";
}
}  // namespace

TEST(start_misuse_is_a_keel_error_with_a_message) {
    {
        auto app = keel::compose<A>();
        CHECK(startError(app) == "keel: start() called before init()");
        app.init();
        app.start();
        CHECK(startError(app) == "keel: start() can be called only once per App (no restart after stop())");
        app.stop();
        CHECK(startError(app) == "keel: start() can be called only once per App (no restart after stop())");
        app.deinit();
        CHECK(startError(app) == "keel: start() called after deinit()");
    }
    {
        auto app = keel::compose<A, B, BadInitC>();
        CHECK_THROWS(app.init());
        CHECK(startError(app) == "keel: start() after a failed init() or start()");
    }
}

namespace {
class Store : public keel::Component<> {
public:
    int value() const { return 42; }
};
class User : public keel::Component<Store> {
public:
    int read() const { return getComponent<Store>().value(); }
};
}  // namespace

namespace {
// Uses its dependency from every hook, and records whether that worked.
class Watcher : public keel::Component<Store> {
public:
    std::vector<int> seen;

private:
    void init() override { seen.push_back(getComponent<Store>().value()); }
    void start() override { seen.push_back(getComponent<Store>().value()); }
    void stop() noexcept override { seen.push_back(getComponent<Store>().value()); }
    void deinit() noexcept override { seen.push_back(getComponent<Store>().value()); }
};
}  // namespace

TEST(dependencies_are_reachable_from_every_hook) {
    auto app = keel::compose<Store, Watcher>();
    app.init();
    app.start();
    app.stop();
    app.deinit();
    CHECK((app.getComponent<Watcher>().seen == std::vector<int>{42, 42, 42, 42}));
}

TEST(dependencies_are_reachable_while_started_and_after_stop) {
    auto app = keel::compose<Store, User>();
    app.init();
    app.start();
    CHECK(app.getComponent<User>().read() == 42);
    app.stop();
    CHECK(app.getComponent<User>().read() == 42);  // still initialized
    app.deinit();
    CHECK_THROWS(app.getComponent<User>().read());
}

TEST(dependencies_are_not_reachable_before_init_or_after_deinit) {
    auto app = keel::compose<Store, User>();
    CHECK_THROWS(app.getComponent<User>().read());  // before init()
    app.init();
    CHECK(app.getComponent<User>().read() == 42);
    app.deinit();
    CHECK_THROWS(app.getComponent<User>().read());  // after deinit()
}

namespace {
// A chain of 100 components, each using the previous one, composed in reverse order:
// the worst case for the compile-time sort.
template <int N>
class Link : public keel::Component<Link<N - 1>> {};
template <>
class Link<0> : public keel::Component<> {};

constexpr int chainLength = 100;

template <std::size_t... Is>
auto reversedChain(std::index_sequence<Is...>) -> keel::App<Link<chainLength - 1 - static_cast<int>(Is)>...>;
using LongChain = decltype(reversedChain(std::make_index_sequence<chainLength>{}));
}  // namespace

TEST(a_long_chain_listed_backwards_composes) {
    LongChain app;
    app.init();
    app.deinit();
}
