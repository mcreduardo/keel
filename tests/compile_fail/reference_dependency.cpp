// Must not compile: Server lists Config& instead of Config.
#include <keel.hpp>

class Config : public keel::Component<> {};
class Server : public keel::Component<Config&> {};

static_assert(!keel::isValid<Config, Server>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Config, Server>();
}
