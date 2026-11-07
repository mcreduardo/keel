// Must not compile: HealthEndpoint comes after every HealthCheck, and Database needs it.
#include <keel.hpp>

struct HealthCheck {
    virtual ~HealthCheck() = default;
};

class HealthEndpoint;
class Database : public keel::Component<HealthEndpoint, keel::Provides<HealthCheck>>, public HealthCheck {};
class HealthEndpoint : public keel::Component<keel::All<HealthCheck>> {};

int main() {
    auto app = keel::compose<HealthEndpoint, Database>();
}
