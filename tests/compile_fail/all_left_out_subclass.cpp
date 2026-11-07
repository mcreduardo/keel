// Must not compile: FakeClock is a subclass of the component Clock, which doesn't provide
// itself, so keel::All<Clock> would silently leave FakeClock out.
#include <keel.hpp>

class Clock : public keel::Component<> {};
class FakeClock : public Clock {};

class Report : public keel::Component<keel::All<Clock>> {};

int main() {
    auto app = keel::compose<Report, Clock, FakeClock>();
}
