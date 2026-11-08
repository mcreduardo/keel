// Must not compile: keel::Alternative<const Cache> could never match a dependency.
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
};

class RedisCache : public keel::Component<>, public keel::Alternative<const Cache>, public Cache,
                   public keel::Conditional {
    bool enabled() const override { return true; }
};

// Rejected where the component is constructed; isValid doesn't see construction.
static_assert(keel::isValid<RedisCache>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<RedisCache>();
}
