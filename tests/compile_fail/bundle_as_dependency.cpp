// Must not compile: Server lists the Core bundle as an InitAfter dependency. A bundle only
// groups components for compose<...>; it isn't a dependency.
#include <keel.hpp>

class Config : public keel::Component<> {};
class Logger : public keel::Component<Config> {};
using Core = keel::Bundle<Config, Logger>;

class Server : public keel::Component<keel::InitAfter<Core>> {};

static_assert(!keel::isValid<Core, Server>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Core, Server>();
}
