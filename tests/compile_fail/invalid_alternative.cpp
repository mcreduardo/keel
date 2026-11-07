// Must not compile: keel::Alternative<const Cache> could never match a dependency.
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
};

class RedisCache : public keel::Component<keel::Alternative<const Cache>>, public Cache {};

static_assert(!keel::isValid<RedisCache>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<RedisCache>();
}
