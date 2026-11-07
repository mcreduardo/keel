// Must not compile: HealthCheck is declared keel::All<HealthCheck>; use getAll.
#include <keel.hpp>

struct HealthCheck {
    virtual ~HealthCheck() = default;
};

class HealthEndpoint : public keel::Component<keel::All<HealthCheck>> {
    void init() override { getComponent<HealthCheck>(); }
};

int main() {}
