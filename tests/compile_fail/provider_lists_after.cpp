// Must not compile: LateCheck provides HealthCheck, its only provider, and lists
// keel::After<HealthCheck>: it would come after itself.
#include <keel.hpp>

struct HealthCheck {
    virtual ~HealthCheck() = default;
};

class LateCheck : public keel::Component<keel::After<HealthCheck>>, public keel::Provides<HealthCheck>,
                  public HealthCheck {};

static_assert(!keel::isValid<LateCheck>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<LateCheck>();
}
