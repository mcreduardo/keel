// Must not compile: MemoryCache derives from keel::Provides<Cache> but not from Cache. No
// component refers to Cache, so the root's getComponent<Cache>() reports it.
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
};

class RedisCache : public keel::Component<>, public keel::Alternative<Cache>, public Cache, public keel::Conditional {
    bool enabled() const override { return true; }
};
class MemoryCache : public keel::Component<>, public keel::Provides<Cache> {};

static_assert(keel::isValid<RedisCache, MemoryCache>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<RedisCache, MemoryCache>();
    app.getComponent<Cache>();
}
