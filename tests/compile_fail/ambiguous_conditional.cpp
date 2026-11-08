// Must not compile: Uplink derives from keel::Conditional twice (directly and through
// Feature), so keel can't tell which enabled() to ask.
#include <keel.hpp>

class Feature : public keel::Conditional {
    bool enabled() const override { return true; }
};

class Uplink : public keel::Component<>, public Feature, public keel::Conditional {
    bool enabled() const override { return true; }
};

static_assert(!keel::isValid<Uplink>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Uplink>();
}
