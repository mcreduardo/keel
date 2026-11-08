// Must not compile, with two errors: Store derives from keel::Provides<Cache> and lists
// keel::Alternative<Cache> (ProvidesAndAlternative), and it isn't a keel::Conditional,
// which is reported at its next alternative, Index (AlternativeNotConditional).
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
};
struct Index {
    virtual ~Index() = default;
};

class Store : public keel::Component<keel::Alternative<Cache>, keel::Alternative<Index>>,
              public keel::Provides<Cache>,
              public Cache,
              public Index {};

static_assert(!keel::isValid<Store>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Store>();
}
