// Must not compile: RedisCache derives from keel::Alternative<Cache> but not from Cache.
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
};

class RedisCache : public keel::Component<>, public keel::Alternative<Cache>, public keel::Conditional {
    bool enabled() const override { return true; }
};
class MemoryCache : public keel::Component<>, public keel::Alternative<Cache>, public Cache, public keel::Conditional {
    bool enabled() const override { return true; }
};

class Repository : public keel::Component<Cache> {};

static_assert(!keel::isValid<Repository, RedisCache, MemoryCache>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Repository, RedisCache, MemoryCache>();
}
