// Must not compile: Store is an alternative of two interfaces without deriving from
// keel::Conditional. One mistake: it is reported once.
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
};
struct Queue {
    virtual ~Queue() = default;
};

class Store : public keel::Component<>, public keel::Alternative<Cache>, public keel::Alternative<Queue>, public Cache,
              public Queue {};

static_assert(!keel::isValid<Store>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Store>();
}
