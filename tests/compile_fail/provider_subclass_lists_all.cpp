// Must not compile: RemoteChecks inherits both keel::Provides<HealthCheck> and the
// keel::All<HealthCheck> entry of AllChecks, so it depends on itself.
#include <keel.hpp>

struct HealthCheck {
    virtual ~HealthCheck() = default;
};

class Database : public keel::Component<>, public keel::Provides<HealthCheck>, public HealthCheck {};
class AllChecks : public keel::Component<keel::All<HealthCheck>>, public keel::Provides<HealthCheck>,
                  public HealthCheck {};
class RemoteChecks : public AllChecks {};

static_assert(!keel::isValid<RemoteChecks, Database>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<RemoteChecks, Database>();
}
