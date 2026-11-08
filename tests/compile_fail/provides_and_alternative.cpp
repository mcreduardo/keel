// Must not compile: RedisCache derives from keel::Provides<Cache> and from
// keel::Alternative<Cache>. Checked because Repository refers to Cache.
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
};

class RedisCache : public keel::Component<>,
                   public keel::Alternative<Cache>,
                   public keel::Provides<Cache>,
                   public Cache,
                   public keel::Conditional {
    bool enabled() const override { return true; }
};
class Repository : public keel::Component<keel::Optional<Cache>> {};

static_assert(!keel::isValid<Repository, RedisCache>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Repository, RedisCache>();
}
