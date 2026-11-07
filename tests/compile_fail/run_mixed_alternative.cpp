// Must not compile: run(&Cache::kind) needs one Cache, and only one of its two providers
// is an alternative.
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
    virtual int kind() { return 0; }
};

class RedisCache : public keel::Component<keel::Alternative<Cache>>, public Cache {};
class MemoryCache : public keel::Component<keel::Provides<Cache>>, public Cache {};

int main() {
    keel::App<RedisCache, MemoryCache> app;
    return app.run(&Cache::kind);
}
