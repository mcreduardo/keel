// Lookups after init() don't allocate: getComponent<T>(), findComponent<T>() (found or
// not), through an interface or an alternative, and App::getComponent<T>(). getAll<I>()
// returns a vector, so it allocates that, once, and nothing else.
//
// Its own executable, because it replaces the global operator new to count allocations.

#include <keel.hpp>

#include "check.hpp"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

namespace {

std::atomic<bool> counting{false};
std::atomic<std::size_t> allocations{0};

void* allocate(std::size_t size) {
    if (counting) ++allocations;
    if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}

// The allocations made by fn.
template <class Fn>
std::size_t allocationsOf(Fn fn) {
    allocations = 0;
    counting = true;
    fn();
    counting = false;
    return allocations;
}

}  // namespace

void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace alloc {

constexpr int rounds = 1000;

class Config : public keel::Component<> {
public:
    bool redis = false;
};

class Logger : public keel::Component<> {};

class Metrics : public keel::Component<> {};

class Cache {
public:
    virtual ~Cache() = default;
};

class RedisCache : public keel::Component<Config, keel::Alternative<Cache>>, public Cache {
    bool enabled() const override { return getComponent<Config>().redis; }
};

class MemoryCache : public keel::Component<Config, keel::Alternative<Cache>>, public Cache {
    bool enabled() const override { return !getComponent<Config>().redis; }
};

class Check {
public:
    virtual ~Check() = default;
};

class DiskCheck : public keel::Component<keel::Provides<Check>>, public Check {};
class NetCheck : public keel::Component<keel::Provides<Check>>, public Check {};

class Plugin {
public:
    virtual ~Plugin() = default;
};

// Looks up its dependencies on demand, as a test would from a running component.
class Server : public keel::Component<Logger, keel::Optional<Metrics>, Cache,
                                      keel::All<Check>, keel::Optional<keel::All<Plugin>>> {
public:
    void lookUpOne(const Logger* logger, const Cache* cache) const {
        CHECK(&getComponent<Logger>() == logger);
        CHECK(findComponent<Metrics>() == nullptr);  // not composed
        CHECK(&getComponent<Cache>() == cache);      // the alternative init() chose
    }
    std::size_t checks() const { return getAll<Check>().size(); }
    std::size_t plugins() const { return getAll<Plugin>().size(); }
};

class Client : public keel::Component<Server, keel::Optional<Logger>> {
public:
    void lookUpOne(const Server* server, const Logger* logger) const {
        CHECK(&getComponent<Server>() == server);
        CHECK(findComponent<Logger>() == logger);  // composed
    }
};

}  // namespace alloc

using namespace alloc;

TEST(lookups_after_init_do_not_allocate) {
    auto app = keel::compose<Config, Logger, RedisCache, MemoryCache, DiskCheck, NetCheck, Server, Client>();
    app.init();
    const Server& server = app.getComponent<Server>();
    const Client& client = app.getComponent<Client>();
    const Logger* logger = &app.getComponent<Logger>();
    const Cache* cache = &app.getComponent<MemoryCache>();  // Config::redis is false
    const std::size_t count = allocationsOf([&] {
        for (int i = 0; i < rounds; ++i) {
            server.lookUpOne(logger, cache);
            client.lookUpOne(&server, logger);
            CHECK(&app.getComponent<Logger>() == logger);
            CHECK(&app.getComponent<Cache>() == cache);  // an alternative, from the root
        }
    });
    CHECK(count == 0);
}

TEST(get_all_allocates_only_its_result) {
    auto app = keel::compose<Config, Logger, RedisCache, MemoryCache, DiskCheck, NetCheck, Server, Client>();
    app.init();
    const Server& server = app.getComponent<Server>();
    std::size_t found = 0;
    CHECK(allocationsOf([&] {
              for (int i = 0; i < rounds; ++i) found += server.checks();
          }) == rounds);  // one vector each
    CHECK(found == 2 * rounds);
    CHECK(allocationsOf([&] {
              for (int i = 0; i < rounds; ++i) found += server.plugins();
          }) == 0);  // no provider: an empty vector
}
