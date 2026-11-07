#include <keel.hpp>

#include "check.hpp"

#include <string>
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
using Server = Part<'S', keel::InitAfter<Migrations>>;  // never uses Migrations

}  // namespace

TEST(init_after_orders_a_composed_component_first) {
    events.clear();
    {
        auto app = keel::compose<Server, Database, Migrations>();  // Server listed first
        app.init();
        app.deinit();
    }
    CHECK((events == std::vector<std::string>{"+D", "+M", "+S", "init D", "init M", "init S", "deinit S",
                                              "deinit M", "deinit D", "-S", "-M", "-D"}));
}

TEST(init_after_an_absent_component_is_ignored) {
    events.clear();
    {
        auto app = keel::compose<Server, Database>();  // no Migrations: Server keeps its place
        app.init();
    }
    CHECK((events == std::vector<std::string>{"+S", "+D", "init S", "init D", "deinit D", "deinit S", "-D", "-S"}));
}

namespace {
using Cache = Part<'C', Database, keel::InitAfter<Server>, keel::Optional<Migrations>>;
}  // namespace

TEST(init_after_combines_with_the_other_dependency_kinds) {
    events.clear();
    {
        auto app = keel::compose<Cache, Server, Migrations, Database>();
        app.init();
    }
    // Database (required) and Migrations (optional) before Cache; Server before Cache (Cache's
    // InitAfter<Server>) and after Migrations (Server's InitAfter<Migrations>).
    CHECK((events == std::vector<std::string>{"+D", "+M", "+S", "+C", "init D", "init M", "init S", "init C",
                                              "deinit C", "deinit S", "deinit M", "deinit D", "-C", "-S", "-M",
                                              "-D"}));
}

TEST(only_the_component_with_init_after_moves) {
    events.clear();
    using Logger = Part<'L'>;
    { auto app = keel::compose<Server, Logger, Database, Migrations>(); }
    // Without the InitAfter edge the order would be S, L, D, M.
    CHECK((events == std::vector<std::string>{"+L", "+D", "+M", "+S", "-S", "-M", "-D", "-L"}));
}

namespace {
using Third = Part<'3'>;
using Second = Part<'2', keel::InitAfter<Third>>;
using First = Part<'1', keel::InitAfter<Second>>;
}  // namespace

TEST(init_after_is_transitive) {
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
using Patient = Part<'P', keel::InitAfter<Never>>;
}  // namespace

TEST(init_after_may_name_an_incomplete_type) {
    events.clear();
    { auto app = keel::compose<Patient>(); }
    CHECK((events == std::vector<std::string>{"+P", "-P"}));
}
