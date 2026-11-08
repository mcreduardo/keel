// Must not compile: AllChecks provides HealthCheck and lists keel::All<HealthCheck>, which
// leaves itself out, and no other provider is composed.
#include <keel.hpp>

struct HealthCheck {
    virtual ~HealthCheck() = default;
};

class AllChecks : public keel::Component<keel::All<HealthCheck>>, public keel::Provides<HealthCheck>, public HealthCheck {};

static_assert(!keel::isValid<AllChecks>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<AllChecks>();
}
