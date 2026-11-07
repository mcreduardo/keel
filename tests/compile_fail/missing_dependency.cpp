// Must not compile: Server needs Logger, and Logger is not composed.
#include <keel.hpp>

class Logger : public keel::Component<> {};
class Server : public keel::Component<Logger> {};

int main() {
    auto app = keel::compose<Server>();
}
