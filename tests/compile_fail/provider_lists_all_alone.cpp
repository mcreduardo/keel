// Must not compile: AllChecks is the only provider of HealthCheck and lists
// keel::All<HealthCheck>: a cycle, not a missing provider.
#include <keel.hpp>

struct HealthCheck {
    virtual ~HealthCheck() = default;
};

class AllChecks : public keel::Component<keel::All<HealthCheck>>, public keel::Provides<HealthCheck>,
                  public HealthCheck {};

static_assert(!keel::isValid<AllChecks>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<AllChecks>();
}
