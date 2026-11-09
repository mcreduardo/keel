// Must not compile: keel::Alternative<Cache> is listed twice (one InvalidDependency).
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
};

class RedisCache : public keel::Component<keel::Alternative<Cache>, keel::Alternative<Cache>>,
                   public Cache,
                   public keel::Conditional {
    bool enabled() const override { return true; }
};

static_assert(!keel::isValid<RedisCache>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<RedisCache>();
}
