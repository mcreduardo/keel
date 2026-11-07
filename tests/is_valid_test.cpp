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
class TwiceProvides : public keel::Component<keel::Provides<Storage>, keel::Alternative<Storage>>, public Storage {};
static_assert(!keel::isValid<Config, Logger, TwiceLogger>);
static_assert(!keel::isValid<TwiceProvides>);

// Missing dependency.
static_assert(!keel::isValid<Logger, Server>);
static_assert(!keel::isValid<Server>);

// Interfaces: missing, ambiguous, mixed, not derived, undeclared provider or subclass.
class DiskStorage : public keel::Component<keel::Provides<Storage>>, public Storage {};
class MemoryStorage : public keel::Component<keel::Provides<Storage>>, public Storage {};
class UsesStorage : public keel::Component<Storage> {};
class MaybeStorage : public keel::Component<keel::Optional<Storage>> {};
class AfterStorage : public keel::Component<keel::InitAfter<Storage>> {};
class NotStorage : public keel::Component<keel::Provides<Storage>> {};
class UndeclaredStorage : public keel::Component<>, public Storage {};

static_assert(keel::isValid<UsesStorage, DiskStorage>);
static_assert(keel::isValid<MaybeStorage>);                               // absent optional
static_assert(keel::isValid<AfterStorage, DiskStorage, MemoryStorage>);  // after both
static_assert(keel::isValid<DiskStorage, MemoryStorage>);                // nothing needs one
static_assert(!keel::isValid<UsesStorage>);
static_assert(!keel::isValid<UsesStorage, DiskStorage, MemoryStorage>);
static_assert(!keel::isValid<MaybeStorage, DiskStorage, MemoryStorage>);
static_assert(!keel::isValid<NotStorage>);
static_assert(!keel::isValid<UsesStorage, UndeclaredStorage>);
static_assert(!keel::isValid<MaybeStorage, UndeclaredStorage>);
static_assert(!keel::isValid<AfterStorage, UndeclaredStorage>);

class Clock : public keel::Component<> {};
class FakeClock : public Clock {};
class UsesClock : public keel::Component<Clock> {};
class SelfProvidingClock : public keel::Component<keel::Provides<SelfProvidingClock>> {};
class FakeSelfProvidingClock : public SelfProvidingClock {};
class UsesSelfProvidingClock : public keel::Component<SelfProvidingClock> {};
static_assert(!keel::isValid<UsesClock, FakeClock>);
static_assert(keel::isValid<UsesSelfProvidingClock, FakeSelfProvidingClock>);

class RedisCache : public keel::Component<keel::Alternative<Cache>>, public Cache {};
class MemoryCache : public keel::Component<keel::Alternative<Cache>>, public Cache {};
class PlainCache : public keel::Component<keel::Provides<Cache>>, public Cache {};
class UsesCache : public keel::Component<Cache> {};
static_assert(keel::isValid<UsesCache, RedisCache, MemoryCache>);
static_assert(!keel::isValid<UsesCache, RedisCache, PlainCache>);
static_assert(keel::isValid<RedisCache, PlainCache>);  // nothing needs one Cache

// Invalid entries.
class ConstOptional : public keel::Component<keel::Optional<const Config>> {};
class NestedOptional : public keel::Component<keel::Optional<keel::Optional<Config>>> {};
class ConstProvides : public keel::Component<keel::Provides<const Storage>>, public Storage {};
static_assert(!keel::isValid<Config, ConstOptional>);
static_assert(!keel::isValid<Config, NestedOptional>);
static_assert(!keel::isValid<ConstProvides>);

// Collections.
class DatabaseCheck : public keel::Component<keel::Provides<HealthCheck>>, public HealthCheck {};
class LeftOutCheck : public keel::Component<>, public HealthCheck {};
class AllChecks : public keel::Component<keel::All<HealthCheck>> {};
class AnyChecks : public keel::Component<keel::Optional<keel::All<HealthCheck>>> {};
class MisnestedChecks : public keel::Component<keel::All<keel::Optional<HealthCheck>>> {};
static_assert(keel::isValid<AllChecks, DatabaseCheck>);
static_assert(keel::isValid<AnyChecks>);
static_assert(!keel::isValid<AllChecks>);
static_assert(!keel::isValid<AllChecks, DatabaseCheck, LeftOutCheck>);
static_assert(!keel::isValid<AnyChecks, LeftOutCheck>);
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
class OwnNames : public keel::Component<Config, keel::Provides<Storage>>, public Storage {
public:
    using Entries = int;
    using RequiredDependencies = keel::Bundle<>;
    using ProvidedInterfaces = void;
    using AlternativeInterfaces = void;
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
