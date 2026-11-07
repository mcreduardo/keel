// Must not compile: keel::All<HealthCheck> needs at least one provider, and none is composed.
#include <keel.hpp>

struct HealthCheck {
    virtual ~HealthCheck() = default;
};

class Database : public keel::Component<> {};

class HealthEndpoint : public keel::Component<keel::All<HealthCheck>> {};

static_assert(!keel::isValid<HealthEndpoint, Database>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<HealthEndpoint, Database>();
}
