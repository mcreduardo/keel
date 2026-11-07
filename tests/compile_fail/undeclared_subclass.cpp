// Must not compile: Service needs a Clock, and only FakeClock, a subclass of the component
// Clock, is composed. Clock doesn't list keel::Provides<Clock>, so FakeClock isn't a Clock to keel.
#include <keel.hpp>

class Clock : public keel::Component<> {};
class FakeClock : public Clock {};

class Service : public keel::Component<keel::Optional<Clock>> {};

static_assert(!keel::isValid<Service, FakeClock>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Service, FakeClock>();
}
