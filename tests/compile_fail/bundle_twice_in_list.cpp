// Must not compile: Server lists the Core bundle twice (as After<Core> and Core). One mistake
// about one type: one InvalidDependency, for the first entry.
#include <keel.hpp>

class Config : public keel::Component<> {};
class Logger : public keel::Component<Config> {};
using Core = keel::Bundle<Config, Logger>;

class Server : public keel::Component<keel::After<Core>, Core> {};

static_assert(!keel::isValid<Core, Server>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Core, Server>();
}
