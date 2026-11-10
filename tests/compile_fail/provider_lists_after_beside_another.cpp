// Must not compile: LateCheck provides HealthCheck and lists keel::After<HealthCheck>, which
// names LateCheck too: a cycle, not an ambiguity, though Database provides it as well.
#include <keel.hpp>

struct HealthCheck {
    virtual ~HealthCheck() = default;
};

class Database : public keel::Component<>, public keel::Provides<HealthCheck>, public HealthCheck {};
class LateCheck : public keel::Component<keel::After<HealthCheck>>, public keel::Provides<HealthCheck>,
                  public HealthCheck {};

static_assert(!keel::isValid<LateCheck, Database>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<LateCheck, Database>();
}
