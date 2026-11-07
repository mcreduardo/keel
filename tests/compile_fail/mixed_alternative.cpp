// Must not compile: Service needs one Cache, and only one of its two providers is an
// alternative, so keel can't choose at runtime.
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
};

class RedisCache : public keel::Component<keel::Alternative<Cache>>, public Cache {};
class MemoryCache : public keel::Component<keel::Provides<Cache>>, public Cache {};

class Service : public keel::Component<Cache> {};

int main() {
    auto app = keel::compose<Service, RedisCache, MemoryCache>();
}
