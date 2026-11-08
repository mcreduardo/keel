// Must not compile: keel::After<keel::Optional<Metrics>> could never match a component.
#include <keel.hpp>

class Metrics : public keel::Component<> {};

class Server : public keel::Component<keel::After<keel::Optional<Metrics>>> {};

static_assert(!keel::isValid<Metrics, Server>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Metrics, Server>();
}
