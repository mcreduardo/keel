// Must not compile: keel::Optional<keel::Optional<Metrics>> could never be found.
#include <keel.hpp>

class Metrics : public keel::Component<> {};

class Server : public keel::Component<keel::Optional<keel::Optional<Metrics>>> {};

int main() {
    auto app = keel::compose<Metrics, Server>();
}
