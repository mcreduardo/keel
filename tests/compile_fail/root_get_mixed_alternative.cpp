// Must not compile: one provider of Cache is an alternative and the other isn't, so the
// root can't get "the" Cache.
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
};

class RedisCache : public keel::Component<keel::Alternative<Cache>>, public Cache {};
class MemoryCache : public keel::Component<keel::Provides<Cache>>, public Cache {};

int main() {
    auto app = keel::compose<RedisCache, MemoryCache>();
    app.getComponent<Cache>();
}
