// Must not compile: keel::Provides<Cache> and keel::Alternative<Cache> in one list.
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
};

class RedisCache : public keel::Component<keel::Provides<Cache>, keel::Alternative<Cache>>, public Cache {};

static_assert(!keel::isValid<RedisCache>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<RedisCache>();
}
