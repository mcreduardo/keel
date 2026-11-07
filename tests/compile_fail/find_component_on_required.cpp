// Must not compile: Metrics is required, so Server must use getComponent<Metrics>().
#include <keel.hpp>

class Metrics : public keel::Component<> {};

class Server : public keel::Component<Metrics> {
    void init() override { findComponent<Metrics>(); }
};

static_assert(keel::isValid<Metrics, Server>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Metrics, Server>();
    app.init();
}
