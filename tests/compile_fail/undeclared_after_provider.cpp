// Must not compile: keel::After<Metrics> would order nothing, since PrometheusMetrics
// derives from Metrics but doesn't derive from keel::Provides<Metrics>.
#include <keel.hpp>

struct Metrics {
    virtual ~Metrics() = default;
};

class PrometheusMetrics : public keel::Component<>, public Metrics {};

class Server : public keel::Component<keel::After<Metrics>> {};

static_assert(!keel::isValid<Server, PrometheusMetrics>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Server, PrometheusMetrics>();
}
