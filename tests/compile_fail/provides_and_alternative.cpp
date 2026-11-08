// Must not compile: RedisCache derives from keel::Provides<Cache> and lists
// keel::Alternative<Cache>.
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
};

class RedisCache : public keel::Component<keel::Alternative<Cache>>, public keel::Provides<Cache>, public Cache {};

static_assert(!keel::isValid<RedisCache>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<RedisCache>();
}
