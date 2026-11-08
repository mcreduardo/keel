#include <keel.hpp>

#include "check.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace {

std::vector<std::string> events;

// Interfaces: plain classes, not components.
struct Storage {
    virtual ~Storage() = default;
    virtual std::string describe() const = 0;
};

struct HealthCheck {
    virtual ~HealthCheck() = default;
    virtual bool healthy() const = 0;
};

// Provides two interfaces. HealthCheck is its second interface base, so a HealthCheck*
// to it differs from a pointer to the component: lookups must return the subobject.
class DiskStorage : public keel::Component<>, public keel::Provides<Storage>, public keel::Provides<HealthCheck>,
                    public Storage,
                    public HealthCheck {
public:
    DiskStorage() { events.push_back("+DiskStorage"); }
    ~DiskStorage() override { events.push_back("-DiskStorage"); }

    std::string describe() const override { return "disk"; }
    bool healthy() const override { return true; }

private:
    void init() override { events.push_back("init DiskStorage"); }
    void deinit() noexcept override { events.push_back("deinit DiskStorage"); }
};

class MemoryStorage : public keel::Component<>, public keel::Provides<Storage>, public Storage {
public:
    std::string describe() const override { return "memory"; }

private:
    void init() override { events.push_back("init MemoryStorage"); }
};

// Depends on whichever Storage is composed.
class Service : public keel::Component<Storage> {
public:
    Service() { events.push_back("+Service"); }
    ~Service() override { events.push_back("-Service"); }

    Storage* storage() const { return storage_; }
    std::string describeNow() const { return getComponent<Storage>().describe(); }

private:
    void init() override {
        storage_ = &getComponent<Storage>();
        events.push_back("init Service, using " + storage_->describe());
    }
    void deinit() noexcept override {
        // Still usable: the provider is deinitialized after this component.
        events.push_back("deinit Service, using " + getComponent<Storage>().describe());
    }
    Storage* storage_ = nullptr;
};

class Monitor : public keel::Component<keel::Optional<HealthCheck>> {
public:
    HealthCheck* check() const { return check_; }

private:
    void init() override {
        check_ = findComponent<HealthCheck>();
        events.push_back("init Monitor");
    }
    HealthCheck* check_ = nullptr;
};

// Runs after every Storage, of which there may be several.
class Backup : public keel::Component<keel::After<Storage>> {
    void init() override { events.push_back("init Backup"); }
    void deinit() noexcept override { events.push_back("deinit Backup"); }
};

std::string errorOf(const Service& service) {
    try {
        service.describeNow();
    } catch (const keel::Error& e) {
        return e.what();
    }
    return "no error";
}

}  // namespace

TEST(a_dependency_on_an_interface_gets_its_provider) {
    events.clear();
    {
        auto app = keel::compose<Service, DiskStorage>();  // the provider listed last
        app.init();
        Storage& viaInterface = app.getComponent<Storage>();
        CHECK(app.getComponent<Service>().storage() == &viaInterface);
        CHECK(&viaInterface == static_cast<Storage*>(&app.getComponent<DiskStorage>()));
        app.deinit();
    }
    // The provider is constructed, initialized, deinitialized and destroyed like a
    // required dependency.
    CHECK((events == std::vector<std::string>{"+DiskStorage", "+Service", "init DiskStorage",
                                              "init Service, using disk", "deinit Service, using disk",
                                              "deinit DiskStorage", "-Service", "-DiskStorage"}));
}

TEST(the_composition_root_picks_the_implementation) {
    events.clear();
    auto app = keel::compose<Service, MemoryStorage>();
    app.init();
    CHECK(app.getComponent<Service>().storage()->describe() == "memory");
}

TEST(the_root_gets_an_interface_subobject_before_init) {
    auto app = keel::compose<DiskStorage>();
    HealthCheck& check = app.getComponent<HealthCheck>();
    CHECK(&check == static_cast<HealthCheck*>(&app.getComponent<DiskStorage>()));
    CHECK(static_cast<void*>(&check) != static_cast<void*>(&app.getComponent<DiskStorage>()));
    CHECK(check.healthy());
    // After init() too, though no component refers to HealthCheck.
    app.init();
    CHECK(&app.getComponent<HealthCheck>() == &check);
}

TEST(an_optional_interface_gets_its_provider_or_nullptr) {
    {
        auto app = keel::compose<Monitor, DiskStorage>();
        app.init();
        CHECK(app.getComponent<Monitor>().check() == &app.getComponent<HealthCheck>());
        CHECK(app.getComponent<Monitor>().check() == static_cast<HealthCheck*>(&app.getComponent<DiskStorage>()));
    }
    {
        auto app = keel::compose<Monitor, MemoryStorage>();  // provides Storage, not HealthCheck
        app.init();
        CHECK(app.getComponent<Monitor>().check() == nullptr);
    }
}

TEST(after_an_interface_follows_every_provider) {
    events.clear();
    {
        auto app = keel::compose<Backup, MemoryStorage, DiskStorage>();
        app.init();
        app.deinit();
    }
    // Backup is constructed and initialized after both providers, and deinitialized and
    // destroyed before them.
    CHECK((events == std::vector<std::string>{"+DiskStorage", "init MemoryStorage", "init DiskStorage",
                                              "init Backup", "deinit Backup", "deinit DiskStorage",
                                              "-DiskStorage"}));
}

TEST(several_providers_are_fine_when_nothing_needs_just_one) {
    auto app = keel::compose<MemoryStorage, DiskStorage>();
    app.init();
    CHECK(app.getComponent<MemoryStorage>().describe() == "memory");
    CHECK(app.getComponent<DiskStorage>().describe() == "disk");
}

TEST(a_lookup_through_an_interface_checks_the_lifecycle) {
    auto app = keel::compose<Service, DiskStorage>();
    const Service& service = app.getComponent<Service>();
    CHECK(errorOf(service).find("Storage before init()") != std::string::npos);
    app.init();
    CHECK(service.describeNow() == "disk");
    app.deinit();
    CHECK(errorOf(service).find("Storage after its deinit()") != std::string::npos);
}

// ---------------------------------------------------------------------------
// run(&I::f): an entry point declared by an interface.

namespace {

struct Shell {
    virtual ~Shell() = default;
    virtual int serve() = 0;
    int banner() const { return 7; }  // not virtual: inherited by the provider
};

class Terminal : public keel::Component<>, public keel::Provides<Shell>, public Shell {
public:
    int serve() override { return 42; }
};

}  // namespace

TEST(run_calls_an_interface_member_on_its_provider) {
    auto app = keel::compose<Terminal>();
    CHECK(app.run(&Shell::serve) == 42);
}

TEST(run_calls_an_inherited_member_on_the_provider) {
    // &Terminal::banner is `int (Shell::*)() const`: found through Provides<Shell>.
    auto app = keel::compose<Terminal>();
    CHECK(app.run(&Terminal::banner) == 7);
}

TEST(keel_run_calls_an_interface_member) {
    CHECK(keel::run<keel::App<Terminal>>(&Shell::serve) == 42);
}

// ---------------------------------------------------------------------------
// A provider that is ordered after the other providers of its own interface.

namespace {

class CachedStorage : public keel::Component<keel::After<HealthCheck>>, public keel::Provides<HealthCheck>,
                      public HealthCheck {
public:
    bool healthy() const override { return true; }

private:
    void init() override { events.push_back("init CachedStorage"); }
};

}  // namespace

TEST(after_its_own_interface_follows_the_other_providers) {
    events.clear();
    auto app = keel::compose<CachedStorage, DiskStorage>();
    app.init();
    CHECK((events == std::vector<std::string>{"+DiskStorage", "init DiskStorage", "init CachedStorage"}));
}

// ---------------------------------------------------------------------------
// Substituting a subclass for a component: the component provides itself.

namespace {

class Clock : public keel::Component<>, public keel::Provides<Clock> {
public:
    virtual int now() const { return 1000; }
};

class FakeClock : public Clock {
public:
    int now() const override { return 1; }
};

class Scheduler : public keel::Component<Clock, keel::Optional<HealthCheck>> {
public:
    int now() const { return clock_->now(); }

private:
    void init() override { clock_ = &getComponent<Clock>(); }
    const Clock* clock_ = nullptr;
};

}  // namespace

TEST(a_component_that_provides_itself_can_be_replaced_by_a_subclass) {
    {
        auto app = keel::compose<Scheduler, Clock>();
        app.init();
        CHECK(app.getComponent<Scheduler>().now() == 1000);
        CHECK(&app.getComponent<Clock>() == &app.getComponent<Clock>());
    }
    {
        auto app = keel::compose<Scheduler, FakeClock>();
        app.init();
        CHECK(app.getComponent<Scheduler>().now() == 1);
        CHECK(&app.getComponent<Clock>() == &app.getComponent<FakeClock>());
    }
}

// ---------------------------------------------------------------------------
// A larger app: 150 components with up to 5 dependencies each, listed backwards. Part<0>
// provides an interface that every other part orders after, and every part provides
// another one, which Census lists with keel::All.

namespace {

struct Node {
    virtual ~Node() = default;
};

struct Counted {
    virtual ~Counted() = default;
    virtual int number() const = 0;
};

constexpr int appSize = 150;

template <int N>
class Part;

// Part<N> uses Part<N-1> ... Part<N-4>, plus After<Node>.
template <int N>
struct PartBase {
    using type = keel::Component<Part<N - 1>, Part<N - 2>, Part<N - 3>, keel::Optional<Part<N - 4>>,
                                 keel::After<Node>>;
};
template <>
struct PartBase<0> {
    using type = keel::Component<>;
};
template <>
struct PartBase<1> {
    using type = keel::Component<Part<0>, keel::After<Node>>;
};
template <>
struct PartBase<2> {
    using type = keel::Component<Part<1>, Part<0>, keel::After<Node>>;
};
template <>
struct PartBase<3> {
    using type = keel::Component<Part<2>, Part<1>, Part<0>, keel::After<Node>>;
};

// Part<0> alone provides Node.
template <int N>
struct NodeProvider {};
template <>
struct NodeProvider<0> : keel::Provides<Node> {};

template <int N>
class Part : public PartBase<N>::type, public NodeProvider<N>, public keel::Provides<Counted>, public Node,
             public Counted {
public:
    int number() const override { return N; }
};

class Census : public keel::Component<keel::All<Counted>> {
public:
    std::vector<int> numbers() const {
        std::vector<int> out;
        for (const Counted* part : getAll<Counted>()) out.push_back(part->number());
        return out;
    }
};

template <std::size_t... Is>
auto reversedParts(std::index_sequence<Is...>) -> keel::App<Census, Part<appSize - 1 - static_cast<int>(Is)>...>;
using LargeApp = decltype(reversedParts(std::make_index_sequence<appSize>{}));

template <class App>
inline constexpr bool validApp = false;
template <class... Cs>
inline constexpr bool validApp<keel::App<Cs...>> = keel::isValid<Cs...>;
static_assert(validApp<LargeApp>);

}  // namespace

TEST(a_large_app_composes) {
    LargeApp app;
    app.init();
    // Every part, in dependency order: Part<N> comes after Part<N-1>.
    std::vector<int> expected;
    for (int n = 0; n < appSize; ++n) expected.push_back(n);
    CHECK(app.getComponent<Census>().numbers() == expected);
    app.deinit();
    CHECK(app.timings().size() == 2 * (appSize + 1));  // init and deinit of each component
}

TEST(a_large_app_draws_its_graph) {
    LargeApp app;
    const std::string dot = app.toDot();
    std::size_t edges = 0;
    for (std::size_t at = dot.find(" -> "); at != std::string::npos; at = dot.find(" -> ", at + 1)) ++edges;
    // Part<N> for N >= 4: three required, one optional, After<Node>; Part<1..3>: 2, 3
    // and 4; Census: one per part.
    CHECK(edges == (appSize - 4) * 5 + 2 + 3 + 4 + appSize);
    CHECK(LargeApp::initOrder().size() == appSize + 1);
}
