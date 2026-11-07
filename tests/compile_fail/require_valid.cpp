// Must not compile: a composition guard with keel::requireValid names the mistake.
#include <keel.hpp>

class Logger : public keel::Component<> {};
class Server : public keel::Component<Logger> {};

static_assert(keel::requireValid<Server>);

int main() {}
