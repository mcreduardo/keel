#include <keel.hpp>

#include "check.hpp"

#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

std::vector<std::string> trace;

struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

template <char Name, bool throwOnInit = false, class... Deps>
class Part : public keel::Component<Deps...> {
    void init() override {
        trace.push_back(std::string("init ") + Name);
        if (throwOnInit) throw Failure(std::string("init ") + Name);
    }
    void deinit() noexcept override { trace.push_back(std::string("deinit ") + Name); }
};

using A = Part<'A'>;
using B = Part<'B', false, A>;
using BadInitC = Part<'C', true, B>;

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
