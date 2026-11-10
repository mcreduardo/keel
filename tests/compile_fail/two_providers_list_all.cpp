// Must not compile: Local and Remote each provide HealthCheck and list
// keel::All<HealthCheck>: each depends on itself and on the other.
#include <keel.hpp>

struct HealthCheck {
    virtual ~HealthCheck() = default;
};

class Local : public keel::Component<keel::All<HealthCheck>>, public keel::Provides<HealthCheck>, public HealthCheck {};
class Remote : public keel::Component<keel::All<HealthCheck>>, public keel::Provides<HealthCheck>,
               public HealthCheck {};

static_assert(!keel::isValid<Local, Remote>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Local, Remote>();
}
