// Must not compile: RedisCache lists keel::Alternative<Cache> but doesn't derive from Cache.
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
};

class RedisCache : public keel::Component<keel::Alternative<Cache>>, public keel::Conditional {
    bool enabled() const override { return true; }
};
class MemoryCache : public keel::Component<keel::Alternative<Cache>>, public Cache, public keel::Conditional {
    bool enabled() const override { return true; }
};

static_assert(!keel::isValid<RedisCache, MemoryCache>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<RedisCache, MemoryCache>();
}
