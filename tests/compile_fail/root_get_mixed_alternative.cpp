// Must not compile: one provider of Cache is an alternative and the other isn't, so the
// root can't get "the" Cache.
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
};

class RedisCache : public keel::Component<>, public keel::Alternative<Cache>, public Cache, public keel::Conditional {
    bool enabled() const override { return true; }
};
class MemoryCache : public keel::Component<>, public keel::Provides<Cache>, public Cache {};

static_assert(keel::isValid<RedisCache, MemoryCache>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<RedisCache, MemoryCache>();
    app.getComponent<Cache>();
}
