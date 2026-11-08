// Must not compile: Uplink's enabled() isn't const, so it doesn't override
// keel::Conditional's `bool enabled() const`, and Uplink is abstract.
#include <keel.hpp>

class Uplink : public keel::Component<>, public keel::Conditional {
    bool enabled() { return true; }
};

static_assert(!keel::isValid<Uplink>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Uplink>();
}
