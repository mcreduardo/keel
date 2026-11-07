// Must not compile: Server needs Logger, and Logger is not composed.
#include <keel.hpp>

class Logger : public keel::Component<> {};
class Server : public keel::Component<Logger> {};

static_assert(!keel::isValid<Server>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Server>();
}
