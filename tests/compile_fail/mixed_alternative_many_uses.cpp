// Must not compile: one provider of Cache lists keel::Provides<Cache> by mistake, and four
// places need one Cache. It is reported once.
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
    virtual int kind() { return 0; }
};

class RedisCache : public keel::Component<keel::Alternative<Cache>>, public Cache {};
class MemoryCache : public keel::Component<keel::Provides<Cache>>, public Cache {};

class Service : public keel::Component<Cache> {};
class Report : public keel::Component<keel::Optional<Cache>> {};

static_assert(!keel::isValid<Service, Report, RedisCache, MemoryCache>, "isValid disagrees with compose");

int main() {
    keel::App<Service, Report, RedisCache, MemoryCache> app;
    app.getComponent<Cache>();
    return app.run(&Cache::kind);
}
