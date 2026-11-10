#include <keel.hpp>

#include "check.hpp"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::string> events;

template <char Name, class... Deps>
class Part : public keel::Component<Deps...> {
public:
    Part() { events.push_back(std::string("+") + Name); }
    ~Part() override { events.push_back(std::string("-") + Name); }

private:
    void init() override { events.push_back(std::string("init ") + Name); }
    void deinit() noexcept override { events.push_back(std::string("deinit ") + Name); }
};

using Database = Part<'D'>;
using Migrations = Part<'M', Database>;
using Server = Part<'S', keel::After<Migrations>>;  // never uses Migrations

}  // namespace

TEST(after_orders_a_composed_component_first) {
    events.clear();
    {
        auto app = keel::compose<Server, Database, Migrations>();  // Server listed first
        app.init();
        app.deinit();
    }
    CHECK((events == std::vector<std::string>{"+D", "+M", "+S", "init D", "init M", "init S", "deinit S",
                                              "deinit M", "deinit D", "-S", "-M", "-D"}));
}

TEST(after_an_absent_component_is_ignored) {
    events.clear();
    {
        auto app = keel::compose<Server, Database>();  // no Migrations: Server keeps its place
        app.init();
    }
    CHECK((events == std::vector<std::string>{"+S", "+D", "init S", "init D", "deinit D", "deinit S", "-D", "-S"}));
}

namespace {
using Cache = Part<'C', Database, keel::After<Server>, keel::Optional<Migrations>>;
}  // namespace

TEST(after_combines_with_the_other_dependency_kinds) {
    events.clear();
    {
        auto app = keel::compose<Cache, Server, Migrations, Database>();
        app.init();
    }
    // Database (required) and Migrations (optional) before Cache; Server before Cache (Cache's
    // After<Server>) and after Migrations (Server's After<Migrations>).
    CHECK((events == std::vector<std::string>{"+D", "+M", "+S", "+C", "init D", "init M", "init S", "init C",
                                              "deinit C", "deinit S", "deinit M", "deinit D", "-C", "-S", "-M",
                                              "-D"}));
}

TEST(only_the_component_with_after_moves) {
    events.clear();
    using Logger = Part<'L'>;
    { auto app = keel::compose<Server, Logger, Database, Migrations>(); }
    // Without the After edge the order would be S, L, D, M.
    CHECK((events == std::vector<std::string>{"+L", "+D", "+M", "+S", "-S", "-M", "-D", "-L"}));
}

namespace {
using Third = Part<'3'>;
using Second = Part<'2', keel::After<Third>>;
using First = Part<'1', keel::After<Second>>;
}  // namespace

TEST(after_is_transitive) {
    events.clear();
    {
        auto app = keel::compose<First, Second, Third>();
        app.init();
    }
    CHECK((events == std::vector<std::string>{"+3", "+2", "+1", "init 3", "init 2", "init 1", "deinit 1",
                                              "deinit 2", "deinit 3", "-1", "-2", "-3"}));
}

namespace {
class Never;  // declared, never defined, never composed
using Patient = Part<'P', keel::After<Never>>;
}  // namespace

TEST(after_may_name_an_incomplete_type) {
    events.clear();
    { auto app = keel::compose<Patient>(); }
    CHECK((events == std::vector<std::string>{"+P", "-P"}));
}

// ---------------------------------------------------------------------------
// After<I> on an interface: its one provider. After<All<I>>: every provider, possibly none.

namespace {

struct Sink {
    virtual ~Sink() = default;
};

template <char Name>
class SinkPart : public Part<Name>, public keel::Provides<Sink>, public Sink {};

using FileSink = SinkPart<'F'>;
using NetSink = SinkPart<'N'>;
using Flusher = Part<'X', keel::After<Sink>>;
using Closer = Part<'Z', keel::After<keel::All<Sink>>>;

}  // namespace

TEST(after_an_interface_orders_after_its_provider) {
    events.clear();
    { auto app = keel::compose<Flusher, FileSink>(); }
    CHECK((events == std::vector<std::string>{"+F", "+X", "-X", "-F"}));
}

TEST(after_all_orders_after_every_provider) {
    events.clear();
    {
        auto app = keel::compose<Closer, FileSink, Database, NetSink>();
        app.init();
        CHECK((app.initOrder() == std::array<std::string_view, 4>{keel::typeName<FileSink>(),
                                                                  keel::typeName<Database>(),
                                                                  keel::typeName<NetSink>(),
                                                                  keel::typeName<Closer>()}));
    }
    CHECK((events == std::vector<std::string>{"+F", "+D", "+N", "+Z", "init F", "init D", "init N", "init Z",
                                              "deinit Z", "deinit N", "deinit D", "deinit F", "-Z", "-N", "-D",
                                              "-F"}));
}

TEST(after_all_without_a_provider_is_ignored) {
    events.clear();
    { auto app = keel::compose<Closer, Database>(); }
    CHECK((events == std::vector<std::string>{"+Z", "+D", "-D", "-Z"}));
}

namespace {

// Alternatives: configuration chooses one at runtime, so After<Sink> comes after each.
template <char Name, bool on>
class AlternativeSink : public Part<Name>, public keel::Alternative<Sink>, public Sink, public keel::Conditional {
    bool enabled() const override { return on; }
};

using LocalSink = AlternativeSink<'L', false>;
using RemoteSink = AlternativeSink<'R', true>;

}  // namespace

TEST(after_an_interface_with_alternatives_orders_after_each) {
    events.clear();
    {
        auto app = keel::compose<Flusher, LocalSink, RemoteSink>();
        app.init();
    }
    CHECK((events == std::vector<std::string>{"+L", "+R", "+X", "init R", "init X", "deinit X", "deinit R", "-X",
                                              "-R", "-L"}));
}
