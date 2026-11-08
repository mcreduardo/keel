// Must not compile: run(&Cache::kind) needs one Cache, and only one of its two providers
// is an alternative.
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
    virtual int kind() { return 0; }
};

class RedisCache : public keel::Component<>, public keel::Alternative<Cache>, public Cache, public keel::Conditional {
    bool enabled() const override { return true; }
};
class MemoryCache : public keel::Component<>, public keel::Provides<Cache>, public Cache {};

static_assert(keel::isValid<RedisCache, MemoryCache>, "isValid disagrees with compose");

int main() {
    keel::App<RedisCache, MemoryCache> app;
    return app.run(&Cache::kind);
}
