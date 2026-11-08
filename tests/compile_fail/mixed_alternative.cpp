// Must not compile: Service needs one Cache, and only one of its two providers is an
// alternative, so keel can't choose at runtime.
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
};

class RedisCache : public keel::Component<>, public keel::Alternative<Cache>, public Cache, public keel::Conditional {
    bool enabled() const override { return true; }
};
class MemoryCache : public keel::Component<>, public keel::Provides<Cache>, public Cache {};

class Service : public keel::Component<Cache> {};

static_assert(!keel::isValid<Service, RedisCache, MemoryCache>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Service, RedisCache, MemoryCache>();
}
