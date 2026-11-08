// Must not compile: Recorder is conditional itself, but it still may not require Uplink,
// which configuration may turn off independently. Depend on keel::Optional<Uplink>.
#include <keel.hpp>

class Uplink : public keel::Component<>, public keel::Conditional {
    bool enabled() const override { return true; }
};

class Recorder : public keel::Component<Uplink>, public keel::Conditional {
    bool enabled() const override { return true; }
};

static_assert(!keel::isValid<Recorder, Uplink>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Recorder, Uplink>();
}
