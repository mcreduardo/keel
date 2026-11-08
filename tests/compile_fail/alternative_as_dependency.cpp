// Must not compile: keel::Alternative<Cache> is listed in keel::Component<...>; it is a base
// class. Repository, which depends on Cache, isn't reported too.
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
};

class RedisCache : public keel::Component<keel::Alternative<Cache>>, public Cache, public keel::Conditional {
    bool enabled() const override { return true; }
};
class Repository : public keel::Component<Cache> {};

static_assert(!keel::isValid<Repository, RedisCache>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Repository, RedisCache>();
}
