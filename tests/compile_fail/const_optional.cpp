// Must not compile: keel::Optional<const Metrics> could never be found; components are
// matched by their exact type.
#include <keel.hpp>

class Metrics : public keel::Component<> {};

class Server : public keel::Component<keel::Optional<const Metrics>> {};

int main() {
    auto app = keel::compose<Metrics, Server>();
}
