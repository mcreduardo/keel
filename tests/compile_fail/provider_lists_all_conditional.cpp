// Must not compile: AllChecks provides HealthCheck and lists keel::All<HealthCheck>. Every
// provider is conditional too, but the cycle is the one error.
#include <keel.hpp>

struct HealthCheck {
    virtual ~HealthCheck() = default;
};

class DiskCheck : public keel::Component<>, public keel::Provides<HealthCheck>, public HealthCheck,
                  public keel::Conditional {
    bool enabled() const override { return true; }
};
class AllChecks : public keel::Component<keel::All<HealthCheck>>, public keel::Provides<HealthCheck>,
                  public HealthCheck, public keel::Conditional {
    bool enabled() const override { return true; }
};

static_assert(!keel::isValid<AllChecks, DiskCheck>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<AllChecks, DiskCheck>();
}
