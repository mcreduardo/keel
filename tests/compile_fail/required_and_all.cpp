// Must not compile: HealthCheck and keel::All<HealthCheck> name the same dependency. The
// getAll call adds no second error.
#include <keel.hpp>

struct HealthCheck {
    virtual ~HealthCheck() = default;
};

class Database : public keel::Component<keel::Provides<HealthCheck>>, public HealthCheck {};

class HealthEndpoint : public keel::Component<HealthCheck, keel::All<HealthCheck>> {
    void init() override { getAll<HealthCheck>(); }
};

int main() {
    auto app = keel::compose<HealthEndpoint, Database>();
}
