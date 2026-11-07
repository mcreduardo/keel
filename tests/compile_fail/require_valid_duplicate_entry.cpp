// Must not compile: Server lists Logger twice. keel::requireValid reports it, without
// constructing anything.
#include <keel.hpp>

class Logger : public keel::Component<> {};
class Server : public keel::Component<Logger, keel::Optional<Logger>> {};

static_assert(keel::requireValid<Logger, Server>);

int main() {}
