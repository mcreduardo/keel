// Must not compile: Server lists Logger both as required and as optional.
#include <keel.hpp>

class Logger : public keel::Component<> {};

class Server : public keel::Component<Logger, keel::Optional<Logger>> {};

static_assert(!keel::isValid<Logger, Server>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Logger, Server>();
}
