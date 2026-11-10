// Must not compile: keel::After<Cache> names one Cache, and only one of its two providers
// is an alternative.
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
};

class RedisCache : public keel::Component<>, public keel::Alternative<Cache>, public Cache, public keel::Conditional {
    bool enabled() const override { return true; }
};
class MemoryCache : public keel::Component<>, public keel::Provides<Cache>, public Cache {};

class Warmup : public keel::Component<keel::After<Cache>> {};

static_assert(!keel::isValid<Warmup, RedisCache, MemoryCache>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Warmup, RedisCache, MemoryCache>();
}
