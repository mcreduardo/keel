// Must not compile: AllChecks provides HealthCheck and lists keel::All<HealthCheck>, which
// leaves itself out, and no other provider is composed.
#include <keel.hpp>

struct HealthCheck {
    virtual ~HealthCheck() = default;
};

class AllChecks : public keel::Component<keel::Provides<HealthCheck>, keel::All<HealthCheck>>, public HealthCheck {};

int main() {
    auto app = keel::compose<AllChecks>();
}
