// Must not compile: MemoryCache derives from keel::Provides<Cache> but not from Cache. The
// only entry that refers to Cache is RedisCache's keel::Alternative<Cache>, which is enough
// for keel to check the providers of Cache.
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
};

class RedisCache : public keel::Component<keel::Alternative<Cache>>, public Cache, public keel::Conditional {
    bool enabled() const override { return true; }
};
class MemoryCache : public keel::Component<>, public keel::Provides<Cache> {};

static_assert(!keel::isValid<RedisCache, MemoryCache>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<RedisCache, MemoryCache>();
}
