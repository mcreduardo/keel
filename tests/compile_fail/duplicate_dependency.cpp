// Must not compile: Server lists Logger twice.
#include <keel.hpp>

class Logger : public keel::Component<> {};

class Server : public keel::Component<Logger, Logger> {};

int main() {
    auto app = keel::compose<Logger, Server>();
}
