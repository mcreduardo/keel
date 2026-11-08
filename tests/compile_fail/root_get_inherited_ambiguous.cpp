// Must not compile: Clock provides itself and FakeClock inherits that, so with both composed
// there are two Clocks, and the root can't get "the" Clock.
#include <keel.hpp>

class Clock : public keel::Component<>, public keel::Provides<Clock> {};
class FakeClock : public Clock {};

static_assert(keel::isValid<Clock, FakeClock>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Clock, FakeClock>();
    app.getComponent<Clock>();
}
