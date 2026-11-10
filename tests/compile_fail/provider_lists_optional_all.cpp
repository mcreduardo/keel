// Must not compile: AnyChecks provides HealthCheck and lists
// keel::Optional<keel::All<HealthCheck>>, which includes AnyChecks itself.
#include <keel.hpp>

struct HealthCheck {
    virtual ~HealthCheck() = default;
};

class AnyChecks : public keel::Component<keel::Optional<keel::All<HealthCheck>>>, public keel::Provides<HealthCheck>,
                  public HealthCheck {};

static_assert(!keel::isValid<AnyChecks>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<AnyChecks>();
}
