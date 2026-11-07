// Must not compile: Cache derives from HealthCheck without keel::Provides<HealthCheck>, so
// keel::All<HealthCheck> would silently leave it out.
#include <keel.hpp>

struct HealthCheck {
    virtual ~HealthCheck() = default;
};

class Database : public keel::Component<keel::Provides<HealthCheck>>, public HealthCheck {};
class Cache : public keel::Component<>, public HealthCheck {};

class HealthEndpoint : public keel::Component<keel::All<HealthCheck>> {};

int main() {
    auto app = keel::compose<HealthEndpoint, Database, Cache>();
}
