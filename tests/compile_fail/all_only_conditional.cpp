// Must not compile: keel::All<HealthCheck> is never empty, and every provider of
// HealthCheck may be turned off. Depend on keel::Optional<keel::All<HealthCheck>>.
#include <keel.hpp>

struct HealthCheck {
    virtual ~HealthCheck() = default;
};

class DiskCheck : public keel::Component<>, public keel::Provides<HealthCheck>, public HealthCheck,
                  public keel::Conditional {
    bool enabled() const override { return true; }
};
class NetCheck : public keel::Component<>, public keel::Provides<HealthCheck>, public HealthCheck,
                 public keel::Conditional {
    bool enabled() const override { return true; }
};

class HealthEndpoint : public keel::Component<keel::All<HealthCheck>> {};

static_assert(!keel::isValid<HealthEndpoint, DiskCheck, NetCheck>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<HealthEndpoint, DiskCheck, NetCheck>();
}
