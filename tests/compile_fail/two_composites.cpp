// Must not compile: each composite lists keel::All<HealthCheck>, so each comes after the
// other.
#include <keel.hpp>

struct HealthCheck {
    virtual ~HealthCheck() = default;
};

class Local : public keel::Component<keel::Provides<HealthCheck>, keel::All<HealthCheck>>, public HealthCheck {};
class Remote : public keel::Component<keel::Provides<HealthCheck>, keel::All<HealthCheck>>, public HealthCheck {};

static_assert(!keel::isValid<Local, Remote>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Local, Remote>();
}
