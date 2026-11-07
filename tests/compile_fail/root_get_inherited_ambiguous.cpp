// Must not compile: Clock provides itself and FakeClock inherits that, so with both composed
// there are two Clocks, and the root can't get "the" Clock.
#include <keel.hpp>

class Clock : public keel::Component<keel::Provides<Clock>> {};
class FakeClock : public Clock {};

int main() {
    auto app = keel::compose<Clock, FakeClock>();
    app.getComponent<Clock>();
}
