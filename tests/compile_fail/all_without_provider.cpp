// Must not compile: keel::All<HealthCheck> needs at least one provider, and none is composed.
#include <keel.hpp>

struct HealthCheck {
    virtual ~HealthCheck() = default;
};

class Database : public keel::Component<> {};

class HealthEndpoint : public keel::Component<keel::All<HealthCheck>> {};

int main() {
    auto app = keel::compose<HealthEndpoint, Database>();
}
