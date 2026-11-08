// Must not compile, with two errors: Store derives from keel::Provides<Cache> and from
// keel::Alternative<Cache> (ProvidesAndAlternative, reported because Repository refers to
// Cache), and it is an alternative without being a keel::Conditional
// (AlternativeNotConditional, reported whether or not something refers to Cache).
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
};

class Store : public keel::Component<>, public keel::Alternative<Cache>, public keel::Provides<Cache>, public Cache {};
class Repository : public keel::Component<keel::Optional<Cache>> {};

static_assert(!keel::isValid<Repository, Store>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Repository, Store>();
}
