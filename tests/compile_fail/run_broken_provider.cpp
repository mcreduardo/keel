// Must not compile: run(&Cache::kind) needs the providers of Cache, and MemoryCache derives
// from keel::Provides<Cache> but not from Cache. No component depends on Cache, so run()
// reports it.
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
    virtual int kind() = 0;
};

class RedisCache : public keel::Component<>, public keel::Alternative<Cache>, public Cache, public keel::Conditional {
public:
    int kind() override { return 1; }

private:
    bool enabled() const override { return true; }
};
class MemoryCache : public keel::Component<>, public keel::Provides<Cache> {};

static_assert(keel::isValid<RedisCache, MemoryCache>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<RedisCache, MemoryCache>();
    return app.run(&Cache::kind);
}
