// Must not compile: findComponent<Metrics>() needs keel::Optional<Metrics> in the list.
#include <keel.hpp>

class Metrics : public keel::Component<> {};

class Server : public keel::Component<> {
    void init() override { findComponent<Metrics>(); }
};

int main() {
    auto app = keel::compose<Metrics, Server>();
    app.init();
}
