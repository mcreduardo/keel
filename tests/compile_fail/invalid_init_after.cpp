// Must not compile: keel::InitAfter<keel::Optional<Metrics>> could never match a component.
#include <keel.hpp>

class Metrics : public keel::Component<> {};

class Server : public keel::Component<keel::InitAfter<keel::Optional<Metrics>>> {};

int main() {
    auto app = keel::compose<Metrics, Server>();
}
