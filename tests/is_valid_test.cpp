// keel::isValid<...>: one composition per check that compose<...> runs, next to a valid
// variant. Every compile-fail test of a composition also checks isValid on its own list
// (tests/compile_fail/), so the two agree on exactly the same types; this file shows that
// each kind of mistake is a plain false, in a translation unit that compiles.
#include <keel.hpp>

#include "check.hpp"

namespace {

struct Storage {
    virtual ~Storage() = default;
};
struct Cache {
    virtual ~Cache() = default;
};
struct HealthCheck {
    virtual ~HealthCheck() = default;
};

class Config : public keel::Component<> {};
class Logger : public keel::Component<Config> {};
class Server : public keel::Component<Logger> {};

// The list itself.
class Settings {};  // not a component
class Hidden : keel::Component<> {};  // private base
class Abstract : public keel::Component<> {
    virtual void handle() = 0;
};
class Undefined;

static_assert(keel::isValid<Config, Logger, Server>);
static_assert(keel::isValid<Server, Logger, Config>);  // any order
static_assert(!keel::isValid<>);
static_assert(!keel::isValid<Config, Settings>);
static_assert(!keel::isValid<Config, Hidden>);
static_assert(!keel::isValid<Config, Abstract>);
static_assert(!keel::isValid<Config, Undefined>);
static_assert(!keel::isValid<Config, Logger, Config>);

// A duplicate in a component's own list (reported by its constructor).
class TwiceLogger : public keel::Component<Logger, keel::Optional<Logger>> {};
class TwiceListedProvides : public keel::Component<keel::Provides<Storage>, keel::Provides<Storage>>, public Storage {};
static_assert(!keel::isValid<Config, Logger, TwiceLogger>);
static_assert(!keel::isValid<TwiceListedProvides>);

// keel::Provides<I> and keel::Alternative<I> are base classes: listed, they are errors.
class ListedProvides : public keel::Component<keel::Provides<Storage>>, public Storage {};
class ListedAlternative : public keel::Component<keel::Alternative<Storage>>, public Storage, public keel::Conditional {
    bool enabled() const override { return true; }
};
static_assert(!keel::isValid<ListedProvides>);
static_assert(!keel::isValid<ListedAlternative>);

// Both bases for one interface: checked when something refers to it.
class ProvidesAndAlternative : public keel::Component<>, public keel::Alternative<Storage>,
                               public keel::Provides<Storage>, public Storage, public keel::Conditional {
    bool enabled() const override { return true; }
};
class PrivateAlternative : public keel::Component<>, keel::Alternative<Storage>, public Storage,
                           public keel::Conditional {
    bool enabled() const override { return true; }
};
class MaybeStorageUser : public keel::Component<keel::Optional<Storage>> {};
static_assert(keel::isValid<ProvidesAndAlternative>);
static_assert(!keel::isValid<MaybeStorageUser, ProvidesAndAlternative>);
static_assert(keel::isValid<PrivateAlternative>);
static_assert(!keel::isValid<MaybeStorageUser, PrivateAlternative>);

// Missing dependency.
static_assert(!keel::isValid<Logger, Server>);
static_assert(!keel::isValid<Server>);

// Interfaces: missing, ambiguous, mixed, not derived, undeclared provider or subclass.
class DiskStorage : public keel::Component<>, public keel::Provides<Storage>, public Storage {};
class MemoryStorage : public keel::Component<>, public keel::Provides<Storage>, public Storage {};
class UsesStorage : public keel::Component<Storage> {};
class MaybeStorage : public keel::Component<keel::Optional<Storage>> {};
class AfterStorage : public keel::Component<keel::InitAfter<Storage>> {};
class NotStorage : public keel::Component<>, public keel::Provides<Storage> {};
struct StorageReader : Storage {};
struct StorageWriter : Storage {};
class AmbiguousDisk : public keel::Component<>, public keel::Provides<Storage>, public StorageReader,
                      public StorageWriter {};  // a Storage& to it would be ambiguous
class PrivateStorage : public keel::Component<>, keel::Provides<Storage>, public Storage {};
class UndeclaredStorage : public keel::Component<>, public Storage {};

static_assert(keel::isValid<UsesStorage, DiskStorage>);
static_assert(keel::isValid<MaybeStorage>);                               // absent optional
static_assert(keel::isValid<AfterStorage, DiskStorage, MemoryStorage>);  // after both
static_assert(keel::isValid<DiskStorage, MemoryStorage>);                // nothing needs one
static_assert(!keel::isValid<UsesStorage>);
static_assert(!keel::isValid<UsesStorage, DiskStorage, MemoryStorage>);
static_assert(!keel::isValid<MaybeStorage, DiskStorage, MemoryStorage>);
static_assert(!keel::isValid<UsesStorage, NotStorage>);
static_assert(!keel::isValid<AfterStorage, NotStorage>);
static_assert(!keel::isValid<UsesStorage, AmbiguousDisk>);
static_assert(!keel::isValid<UsesStorage, PrivateStorage>);
static_assert(!keel::isValid<MaybeStorage, PrivateStorage>);
static_assert(!keel::isValid<UsesStorage, DiskStorage, PrivateStorage>);
// keel asks the components only about the types something refers to (C++20 can't list a
// class's bases): a keel::Provides<Storage> that nothing needs isn't checked.
static_assert(keel::isValid<NotStorage>);
static_assert(keel::isValid<PrivateStorage>);
static_assert(!keel::isValid<UsesStorage, UndeclaredStorage>);
static_assert(!keel::isValid<MaybeStorage, UndeclaredStorage>);
static_assert(!keel::isValid<AfterStorage, UndeclaredStorage>);

class Clock : public keel::Component<> {};
class FakeClock : public Clock {};
class UsesClock : public keel::Component<Clock> {};
class SelfProvidingClock : public keel::Component<>, public keel::Provides<SelfProvidingClock> {};
class FakeSelfProvidingClock : public SelfProvidingClock {};
class UsesSelfProvidingClock : public keel::Component<SelfProvidingClock> {};
static_assert(!keel::isValid<UsesClock, FakeClock>);
static_assert(keel::isValid<UsesSelfProvidingClock, FakeSelfProvidingClock>);

class RedisCache : public keel::Component<>, public keel::Alternative<Cache>, public Cache, public keel::Conditional {
    bool enabled() const override { return true; }
};
class MemoryCache : public keel::Component<>, public keel::Alternative<Cache>, public Cache, public keel::Conditional {
    bool enabled() const override { return false; }
};
class PlainCache : public keel::Component<>, public keel::Provides<Cache>, public Cache {};
class UsesCache : public keel::Component<Cache> {};
class MaybeCache : public keel::Component<keel::Optional<Cache>> {};
class AnyCaches : public keel::Component<keel::Optional<keel::All<Cache>>> {};
class AllCaches : public keel::Component<keel::All<Cache>> {};
class UnconditionalCache : public keel::Component<>, public keel::Alternative<Cache>, public Cache {};
static_assert(keel::isValid<UsesCache, RedisCache, MemoryCache>);
static_assert(keel::isValid<MaybeCache, RedisCache, MemoryCache>);
static_assert(keel::isValid<AnyCaches, RedisCache, MemoryCache>);
static_assert(!keel::isValid<AllCaches, RedisCache, MemoryCache>);  // every provider is conditional
static_assert(keel::isValid<AllCaches, RedisCache, PlainCache>);    // PlainCache is always there
static_assert(!keel::isValid<UsesCache, RedisCache, PlainCache>);
static_assert(keel::isValid<RedisCache, PlainCache>);  // nothing needs one Cache
// An alternative of keel::Conditional itself, next to a dependency on keel::Conditional:
// the passed checks of the two are distinct (isValid and compose agree).
class AlternativeOfConditional : public keel::Component<>, public keel::Alternative<keel::Conditional>,
                                 public keel::Conditional {
    bool enabled() const override { return true; }
};
class UsesConditional : public keel::Component<keel::Conditional> {};
static_assert(keel::isValid<AlternativeOfConditional, UsesConditional>);
static_assert(keel::requireValid<AlternativeOfConditional, UsesConditional>);
static_assert(!keel::isValid<UnconditionalCache>);  // even though nothing refers to Cache

// Conditional components: nothing may require one.
class Uplink : public keel::Component<Config>, public keel::Conditional {
    bool enabled() const override { return true; }
};
class NeedsUplink : public keel::Component<Uplink> {};
class MaybeUplink : public keel::Component<keel::Optional<Uplink>> {};
class AfterUplink : public keel::Component<keel::InitAfter<Uplink>> {};
class ConditionalNeedsUplink : public keel::Component<Uplink>, public keel::Conditional {
    bool enabled() const override { return true; }
};
class ConditionalDisk : public keel::Component<>, public keel::Provides<Storage>, public Storage,
                        public keel::Conditional {
    bool enabled() const override { return true; }
};
class PrivateConditional : public keel::Component<>, keel::Conditional {
    bool enabled() const override { return true; }
};
class OwnEnabled : public keel::Component<> {  // not conditional: its enabled() is its own
public:
    bool enabled() const { return false; }
};
class NeedsOwnEnabled : public keel::Component<OwnEnabled> {};
static_assert(keel::isValid<Uplink, Config>);
static_assert(!keel::isValid<NeedsUplink, Uplink, Config>);
static_assert(keel::isValid<MaybeUplink, Uplink, Config>);
static_assert(keel::isValid<AfterUplink, Uplink, Config>);
static_assert(!keel::isValid<ConditionalNeedsUplink, Uplink, Config>);
static_assert(!keel::isValid<UsesStorage, ConditionalDisk>);
static_assert(keel::isValid<MaybeStorage, ConditionalDisk>);
static_assert(!keel::isValid<PrivateConditional>);
static_assert(keel::isValid<NeedsOwnEnabled, OwnEnabled>);

// A concrete alternative is conditional: requiring it by its own type doesn't compile.
class NeedsRedis : public keel::Component<RedisCache> {};
static_assert(!keel::isValid<NeedsRedis, RedisCache, MemoryCache>);

// keel::Conditional inherited through a user's template base is detected.
template <class... Deps>
class Toggle : public keel::Component<Deps...>, public keel::Conditional {};
class Feature : public Toggle<Config> {
    bool enabled() const override { return true; }
};
class NeedsFeature : public keel::Component<Feature> {};
class MaybeFeature : public keel::Component<keel::Optional<Feature>> {};
static_assert(!keel::isValid<NeedsFeature, Feature, Config>);
static_assert(keel::isValid<MaybeFeature, Feature, Config>);

// An interface that derives from keel::Conditional makes its providers conditional.
struct Light : public keel::Conditional {
    virtual ~Light() = default;
};
class Lamp : public keel::Component<>, public keel::Provides<Light>, public Light {
    bool enabled() const override { return true; }
};
class UsesLight : public keel::Component<Light> {};
class MaybeLight : public keel::Component<keel::Optional<Light>> {};
static_assert(!keel::isValid<UsesLight, Lamp>);
static_assert(keel::isValid<MaybeLight, Lamp>);

// A conditional component inside a bundle is checked like any other.
using Uplinks = keel::Bundle<Uplink, Config>;
static_assert(!keel::isValid<Uplinks, NeedsUplink>);
static_assert(keel::isValid<Uplinks, MaybeUplink>);

// Invalid entries.
class ConstOptional : public keel::Component<keel::Optional<const Config>> {};
class NestedOptional : public keel::Component<keel::Optional<keel::Optional<Config>>> {};
static_assert(!keel::isValid<Config, ConstOptional>);
// keel::Provides<const Storage> (or any I no dependency could name) is rejected where the
// component is constructed (tests/compile_fail/invalid_provides.cpp). isValid doesn't see
// construction, nor an I nothing refers to, so it says true.
class ConstProvides : public keel::Component<>, public keel::Provides<const Storage>, public Storage {};
static_assert(keel::isValid<ConstProvides>);
static_assert(!keel::isValid<Config, NestedOptional>);

// Collections.
class DatabaseCheck : public keel::Component<>, public keel::Provides<HealthCheck>, public HealthCheck {};
class LeftOutCheck : public keel::Component<>, public HealthCheck {};
class ConditionalCheck : public keel::Component<>, public keel::Provides<HealthCheck>, public HealthCheck,
                         public keel::Conditional {
    bool enabled() const override { return true; }
};
class AllChecks : public keel::Component<keel::All<HealthCheck>> {};
class AnyChecks : public keel::Component<keel::Optional<keel::All<HealthCheck>>> {};
class MisnestedChecks : public keel::Component<keel::All<keel::Optional<HealthCheck>>> {};
static_assert(keel::isValid<AllChecks, DatabaseCheck>);
static_assert(keel::isValid<AnyChecks>);
static_assert(!keel::isValid<AllChecks>);
static_assert(!keel::isValid<AllChecks, DatabaseCheck, LeftOutCheck>);
static_assert(!keel::isValid<AnyChecks, LeftOutCheck>);
static_assert(!keel::isValid<AllChecks, ConditionalCheck>);
static_assert(keel::isValid<AllChecks, ConditionalCheck, DatabaseCheck>);
static_assert(keel::isValid<AnyChecks, ConditionalCheck>);
// A composite that provides HealthCheck itself doesn't count for its own All<HealthCheck>:
// the only other provider is conditional.
class CompositeCheck : public keel::Component<keel::All<HealthCheck>>, public keel::Provides<HealthCheck>,
                       public HealthCheck {};
static_assert(!keel::isValid<CompositeCheck, ConditionalCheck>);
static_assert(keel::isValid<CompositeCheck, ConditionalCheck, DatabaseCheck>);
static_assert(!keel::isValid<MisnestedChecks, DatabaseCheck>);

// Cycles.
class P;
class Q;
class P : public keel::Component<Q> {};
class Q : public keel::Component<keel::InitAfter<P>> {};
static_assert(!keel::isValid<P, Q>);

// Types that can't be components or dependencies at all: still a plain false.
class ReferenceEntry : public keel::Component<Config&> {};
class ConstEntry : public keel::Component<const Config> {};
static_assert(!keel::isValid<Config&>);
static_assert(!keel::isValid<void() const>);
static_assert(!keel::isValid<int>);
static_assert(!keel::isValid<keel::Bundle<int&>>);
static_assert(!keel::isValid<Config, ReferenceEntry>);
static_assert(!keel::isValid<Config, ConstEntry>);

// A class that derives from keel's internal base, but not from keel::Component<...>.
class Raw : public keel::detail::ComponentBase {};
static_assert(!keel::isValid<Raw>);

// Members named like keel::Component's own don't confuse keel.
class OwnNames : public keel::Component<Config>, public keel::Provides<Storage>, public Storage {
public:
    using Entries = int;
    using RequiredDependencies = keel::Bundle<>;
    using ListedProvides = void;
    using ListedAlternatives = void;
    [[maybe_unused]] static constexpr bool uniqueEntries = false;
};
static_assert(keel::isValid<Config, OwnNames, UsesStorage>);
static_assert(!keel::isValid<OwnNames>);  // still needs Config

// Bundles: as a dependency, or reached twice.
using Core = keel::Bundle<Config, Logger>;
class NeedsCore : public keel::Component<Core> {};
class AfterCore : public keel::Component<keel::InitAfter<Core>> {};
class MaybeCore : public keel::Component<keel::Optional<Core>> {};
static_assert(!keel::isValid<Core, NeedsCore>);
static_assert(!keel::isValid<Core, AfterCore>);
static_assert(!keel::isValid<Core, MaybeCore>);
static_assert(keel::isValid<Core, Server>);
static_assert(!keel::isValid<Core, Core>);
static_assert(!keel::isValid<Core, keel::Bundle<Core, Server>>);
static_assert(!keel::isValid<const Core>);

// Several mistakes at once.
static_assert(!keel::isValid<Server, UsesStorage, P, Q>);

// Constructor arguments are not part of the check: compose<...>(args...) supplies them.
class Port : public keel::Component<> {
public:
    explicit Port(int number) : number_(number) {}
    int number() const { return number_; }

private:
    int number_;
};
static_assert(keel::isValid<Port>);

// The reporting form: true, or the error.
static_assert(keel::requireValid<Config, Logger, Server>);
static_assert(keel::requireValid<Core, Server, Port>);

}  // namespace

TEST(is_valid_is_an_ordinary_constant) {
    constexpr bool production = keel::isValid<UsesStorage, DiskStorage>;
    constexpr bool broken = keel::isValid<UsesStorage>;
    CHECK(production);
    CHECK(!broken);
    auto app = keel::compose<Port>(8080);
    CHECK(app.getComponent<Port>().number() == 8080);
}

TEST(members_named_like_keels_dont_matter) {
    auto app = keel::compose<UsesStorage, OwnNames, Config>();
    app.init();
    CHECK(&app.getComponent<Storage>() == static_cast<Storage*>(&app.getComponent<OwnNames>()));
}
