// Must not compile: LateCheck provides HealthCheck and lists
// keel::After<keel::All<HealthCheck>>, which includes LateCheck itself.
#include <keel.hpp>

struct HealthCheck {
    virtual ~HealthCheck() = default;
};

class Database : public keel::Component<>, public keel::Provides<HealthCheck>, public HealthCheck {};
class LateCheck : public keel::Component<keel::After<keel::All<HealthCheck>>>, public keel::Provides<HealthCheck>,
                  public HealthCheck {};

static_assert(!keel::isValid<Database, LateCheck>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Database, LateCheck>();
}
