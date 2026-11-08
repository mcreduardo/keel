#include <keel.hpp>

#include "check.hpp"

#include <chrono>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Components in a named namespace, so that their names are the same on every compiler.
namespace intro {

// ---------------------------------------------------------------------------
// initOrder() and timings()

struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

enum class Behavior { normal, failInit, failStart, slowInit };

template <char Name, Behavior behavior = Behavior::normal, class... Deps>
class Part : public keel::Component<Deps...> {
    void init() override {
        if (behavior == Behavior::failInit) throw Failure("init");
        if (behavior == Behavior::slowInit) {
            // Busy-waits on the clock keel measures with, so the recorded duration is at
            // least 1 ms however the thread is scheduled.
            const auto begin = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - begin < std::chrono::milliseconds(1)) {
            }
        }
    }
    void start() override {
        if (behavior == Behavior::failStart) throw Failure("start");
    }
};

// A component that configuration turned off.
template <char Name, class... Deps>
class OffPart : public keel::Component<Deps...>, public keel::Conditional {
    bool enabled() const override { return false; }
};

using A = Part<'A'>;
using B = Part<'B', Behavior::normal, A>;
using C = Part<'C', Behavior::normal, B>;

struct Entry {
    std::string component;
    keel::Phase phase;
    bool failed;
    bool operator==(const Entry&) const = default;
};

std::vector<Entry> entries(std::span<const keel::Timing> timings) {
    std::vector<Entry> out;
    for (const keel::Timing& t : timings) out.push_back(Entry{std::string(t.component), t.phase, t.failed});
    return out;
}

}  // namespace intro

using intro::A;
using intro::B;
using intro::C;
using intro::Behavior;
using intro::Part;
using Phase = keel::Phase;

// The order is a compile-time constant: dependencies first, the list order otherwise.
static_assert(keel::App<C, B, A>::initOrder().size() == 3);
static_assert(keel::App<C, B, A>::initOrder()[0] == keel::typeName<A>());
static_assert(keel::App<C, B, A>::initOrder()[1] == keel::typeName<B>());
static_assert(keel::App<C, B, A>::initOrder()[2] == keel::typeName<C>());
static_assert(keel::App<keel::Bundle<C>, keel::Bundle<A, B>>::initOrder()[0] == keel::typeName<A>());

TEST(init_order_names_the_components_in_init_order) {
    auto app = keel::compose<C, B, A>();
    constexpr auto order = decltype(app)::initOrder();
    CHECK(order[0] == app.getComponent<A>().componentName());
    CHECK(order[1] == app.getComponent<B>().componentName());
    CHECK(order[2] == app.getComponent<C>().componentName());
    CHECK(app.initOrder() == order);
}

TEST(init_order_includes_disabled_components) {
    using Off = intro::OffPart<'O', A>;
    auto app = keel::compose<Off, A>();
    app.init();
    CHECK(app.initOrder().size() == 2);
    CHECK(app.initOrder()[1] == keel::typeName<Off>());
}

TEST(timings_record_every_lifecycle_call_in_call_order) {
    auto app = keel::compose<C, B, A>();
    CHECK(app.timings().empty());
    app.init();
    app.start();
    app.stop();
    app.deinit();
    const std::string a(keel::typeName<A>()), b(keel::typeName<B>()), c(keel::typeName<C>());
    CHECK((intro::entries(app.timings()) ==
           std::vector<intro::Entry>{{a, Phase::init, false},   {b, Phase::init, false},   {c, Phase::init, false},
                                     {a, Phase::start, false},  {b, Phase::start, false},  {c, Phase::start, false},
                                     {c, Phase::stop, false},   {b, Phase::stop, false},   {a, Phase::stop, false},
                                     {c, Phase::deinit, false}, {b, Phase::deinit, false}, {a, Phase::deinit, false}}));
    for (const keel::Timing& t : app.timings()) CHECK(t.duration >= std::chrono::nanoseconds(0));
    app.deinit();  // nothing more to record
    CHECK(app.timings().size() == 12);
}

TEST(timings_measure_the_hook) {
    using Slow = Part<'S', Behavior::slowInit>;
    auto app = keel::compose<Slow>();
    app.init();
    CHECK(app.timings().size() == 1);
    CHECK(app.timings()[0].duration >= std::chrono::milliseconds(1));
}

TEST(a_failed_init_is_recorded_before_the_rollback) {
    using Bad = Part<'X', Behavior::failInit, B>;
    auto app = keel::compose<Bad, B, A>();
    CHECK_THROWS(app.init());
    const std::string a(keel::typeName<A>()), b(keel::typeName<B>()), x(keel::typeName<Bad>());
    CHECK((intro::entries(app.timings()) == std::vector<intro::Entry>{{a, Phase::init, false},
                                                                      {b, Phase::init, false},
                                                                      {x, Phase::init, true},
                                                                      {b, Phase::deinit, false},
                                                                      {a, Phase::deinit, false}}));
}

TEST(a_failed_start_is_recorded_before_the_rollback) {
    using Bad = Part<'X', Behavior::failStart, B>;
    auto app = keel::compose<Bad, B, A>();
    app.init();
    CHECK_THROWS(app.start());
    const std::string a(keel::typeName<A>()), b(keel::typeName<B>()), x(keel::typeName<Bad>());
    CHECK((intro::entries(app.timings()) ==
           std::vector<intro::Entry>{{a, Phase::init, false},   {b, Phase::init, false},   {x, Phase::init, false},
                                     {a, Phase::start, false},  {b, Phase::start, false},  {x, Phase::start, true},
                                     {b, Phase::stop, false},   {a, Phase::stop, false},   {x, Phase::deinit, false},
                                     {b, Phase::deinit, false}, {a, Phase::deinit, false}}));
}

TEST(a_disabled_component_has_no_timings) {
    using Off = intro::OffPart<'O', A>;
    auto app = keel::compose<Off, A>();
    app.run([] {});
    const std::string a(keel::typeName<A>());
    CHECK((intro::entries(app.timings()) == std::vector<intro::Entry>{{a, Phase::init, false},
                                                                      {a, Phase::start, false},
                                                                      {a, Phase::stop, false},
                                                                      {a, Phase::deinit, false}}));
}

TEST(phases_have_names) {
    CHECK(keel::toString(Phase::init) == "init");
    CHECK(keel::toString(Phase::start) == "start");
    CHECK(keel::toString(Phase::stop) == "stop");
    CHECK(keel::toString(Phase::deinit) == "deinit");
}

// ---------------------------------------------------------------------------
// toDot()

namespace intro {

struct Storage {
    virtual ~Storage() = default;
};
struct Cache {
    virtual ~Cache() = default;
};
struct Check {
    virtual ~Check() = default;
};
struct Tracing;  // never composed

class Config : public keel::Component<> {};
class Disk : public keel::Component<Config, keel::Provides<Storage>>, public Storage {};
class Memory : public keel::Component<keel::Alternative<Cache>>, public Cache, public keel::Conditional {
    bool enabled() const override { return true; }
};
class Redis : public keel::Component<keel::Alternative<Cache>>, public Cache, public keel::Conditional {
    bool enabled() const override { return false; }
};
class Migrations : public keel::Component<> {};
class Metrics : public keel::Component<> {};
class Ping : public keel::Component<keel::Provides<Check>>, public Check {};
class Plugins : public keel::Component<keel::Optional<keel::All<Check>>> {};
class Server : public keel::Component<Storage, Cache, keel::Optional<Metrics>, keel::Optional<Tracing>,
                                      keel::InitAfter<Migrations>, keel::All<Check>> {};

// The labels of a DOT text, unescaped: \n is a line break, \x is x.
std::vector<std::string> labels(const std::string& dot) {
    std::vector<std::string> out;
    const std::string start = "label=\"";
    for (std::size_t at = dot.find(start); at != std::string::npos; at = dot.find(start, at)) {
        std::string label;
        std::size_t i = at + start.size();
        for (; i < dot.size() && dot[i] != '"'; ++i) {
            if (dot[i] == '\\' && i + 1 < dot.size()) {
                ++i;
                label += dot[i] == 'n' ? '\n' : dot[i];
            } else {
                label += dot[i];
            }
        }
        out.push_back(label);
        at = i;
    }
    return out;
}

template <class T>
class Named : public keel::Component<T> {};
template <char Name>
class Tag : public keel::Component<> {};
template <class T>
class Holder : public keel::Component<> {};

template <std::size_t N>
struct Text {
    constexpr Text(const char (&text)[N]) {
        for (std::size_t i = 0; i < N; ++i) chars[i] = text[i];
    }
    char chars[N];
};
template <Text text>
class Labelled : public keel::Component<> {};

}  // namespace intro

TEST(to_dot_draws_components_and_edges) {
    using namespace intro;
    auto app = keel::compose<Server, Plugins, Ping, Memory, Redis, Disk, Config, Migrations, Metrics>();
    // Nodes and edges in init order (Ping, Plugins, Memory, Redis, Config, Disk,
    // Migrations, Metrics, Server); node n<i> is the i-th component of the list.
    const std::string expected = R"(digraph keel {
  rankdir=LR;
  node [shape=box];
  n2 [label="intro::Ping\nprovides intro::Check"];
  n1 [label="intro::Plugins"];
  n3 [label="intro::Memory\nalternative intro::Cache"];
  n4 [label="intro::Redis\nalternative intro::Cache"];
  n6 [label="intro::Config"];
  n5 [label="intro::Disk\nprovides intro::Storage"];
  n7 [label="intro::Migrations"];
  n8 [label="intro::Metrics"];
  n0 [label="intro::Server"];
  n1 -> n2 [style="bold,dashed", label="intro::Check"];
  n5 -> n6;
  n0 -> n5 [label="intro::Storage"];
  n0 -> n3 [label="intro::Cache"];
  n0 -> n4 [label="intro::Cache"];
  n0 -> n8 [style=dashed];
  n0 -> n7 [style=dotted, arrowhead=empty];
  n0 -> n2 [style=bold, label="intro::Check"];
}
)";
    CHECK(app.toDot() == expected);

    // After init(), the disabled alternative is gray; its edges stay.
    app.init();
    std::string disabled = expected;
    const std::string redis = R"(n4 [label="intro::Redis\nalternative intro::Cache"];)";
    disabled.replace(disabled.find(redis), redis.size(),
                     R"x(n4 [label="intro::Redis\nalternative intro::Cache\n(disabled)", color=gray, fontcolor=gray];)x");
    CHECK(app.toDot() == disabled);
}

TEST(to_dot_escapes_names) {
    using namespace intro;
    using Quote = Tag<'"'>;
    using Backslash = Tag<'\\'>;
    using Nested = Named<Quote>;
    auto app = keel::compose<Nested, Quote, Backslash>();
    const std::string dot = app.toDot();
    const std::string quote(keel::typeName<Quote>());
    const std::string backslash(keel::typeName<Backslash>());
    const std::string nested(keel::typeName<Nested>());
    CHECK((labels(dot) == std::vector<std::string>{quote, nested, backslash}));
    // Spelled as clang and GCC print them, if they do.
    if (quote == R"(intro::Tag<'"'>)") CHECK(dot.find(R"(label="intro::Tag<'\"'>")") != std::string::npos);
    if (backslash == R"(intro::Tag<'\\'>)") CHECK(dot.find(R"(label="intro::Tag<'\\\\'>")") != std::string::npos);
}

// Names may contain ']' and ';' (arrays, string template arguments); each name is whole.
static_assert(keel::typeName<intro::Holder<int[3]>>() != keel::typeName<intro::Holder<int[3][2]>>());
static_assert(keel::typeName<intro::Holder<int[3]>>().ends_with(">"));
static_assert(keel::typeName<intro::Labelled<"a];b">>().ends_with(">"));

TEST(names_with_brackets_are_whole) {
    using namespace intro;
    using App = keel::App<Holder<int[3]>, Holder<int[3][2]>, Labelled<"a];b">, Labelled<"a]">>;
    constexpr auto order = App::initOrder();
    CHECK(order[0] != order[1]);
    CHECK(order[2] != order[3]);
    for (std::string_view name : order) CHECK(name.ends_with(">"));
    App app;
    CHECK((labels(app.toDot()) == std::vector<std::string>{std::string(order[0]), std::string(order[1]),
                                                             std::string(order[2]), std::string(order[3])}));
    // Spelled as clang prints them, if it does.
    if (order[0] == "intro::Holder<int[3]>") CHECK(order[1] == "intro::Holder<int[3][2]>");
}

