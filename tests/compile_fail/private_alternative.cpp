// Must not compile: RedisCache derives from keel::Alternative<Cache> privately (the default
// for a class), so keel can't see that it is an alternative of Cache. Reported once, not as
// a missing dependency of Repository too.
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
};

class RedisCache : public keel::Component<>, keel::Alternative<Cache>, public Cache, public keel::Conditional {
    bool enabled() const override { return true; }
};
class Repository : public keel::Component<Cache> {};

static_assert(!keel::isValid<Repository, RedisCache>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Repository, RedisCache>();
}
