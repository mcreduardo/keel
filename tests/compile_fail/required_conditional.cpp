// Must not compile: Dashboard requires Uplink, which configuration may turn off. Depend on
// keel::Optional<Uplink> instead.
#include <keel.hpp>

class Config : public keel::Component<> {};

class Uplink : public keel::Component<Config>, public keel::Conditional {
    bool enabled() const override { return true; }
};

class Dashboard : public keel::Component<Uplink> {};

static_assert(!keel::isValid<Dashboard, Uplink, Config>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Dashboard, Uplink, Config>();
}
