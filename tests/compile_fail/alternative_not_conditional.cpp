// Must not compile: RedisCache derives from keel::Alternative<Cache> but not from
// keel::Conditional, so configuration can't turn it off. Its own enabled() is just a member.
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
};

class RedisCache : public keel::Component<>, public keel::Alternative<Cache>, public Cache {
public:
    bool enabled() const { return true; }
};
class MemoryCache : public keel::Component<>, public keel::Alternative<Cache>, public Cache, public keel::Conditional {
    bool enabled() const override { return false; }
};

class Repository : public keel::Component<Cache> {};

static_assert(!keel::isValid<Repository, RedisCache, MemoryCache>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Repository, RedisCache, MemoryCache>();
}
