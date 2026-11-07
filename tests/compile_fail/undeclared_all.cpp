// Must not compile: getAll<HealthCheck>() without keel::All<HealthCheck> in the list.
#include <keel.hpp>

struct HealthCheck {
    virtual ~HealthCheck() = default;
};

class HealthEndpoint : public keel::Component<> {
    void init() override { getAll<HealthCheck>(); }
};

int main() {}
