// Must not compile: AllChecks provides HealthCheck and lists keel::All<HealthCheck>, which
// includes AllChecks itself: it depends on itself, whatever the other providers.
#include <keel.hpp>

struct HealthCheck {
    virtual ~HealthCheck() = default;
};

class Database : public keel::Component<>, public keel::Provides<HealthCheck>, public HealthCheck {};
class AllChecks : public keel::Component<keel::All<HealthCheck>>, public keel::Provides<HealthCheck>,
                  public HealthCheck {};

static_assert(!keel::isValid<AllChecks, Database>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<AllChecks, Database>();
}
