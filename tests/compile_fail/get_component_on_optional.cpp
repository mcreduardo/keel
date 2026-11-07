// Must not compile: Metrics is optional, so Server must use findComponent<Metrics>().
#include <keel.hpp>

class Metrics : public keel::Component<> {};

class Server : public keel::Component<keel::Optional<Metrics>> {
    void init() override { getComponent<Metrics>(); }
};

static_assert(keel::isValid<Metrics, Server>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Metrics, Server>();
    app.init();
}
