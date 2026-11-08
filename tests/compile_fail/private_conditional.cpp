// Must not compile: Uplink derives from keel::Conditional privately (the default for a
// class), so keel can't ask its enabled().
#include <keel.hpp>

class Uplink : public keel::Component<>, keel::Conditional {
    bool enabled() const override { return true; }
};

static_assert(!keel::isValid<Uplink>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Uplink>();
}
