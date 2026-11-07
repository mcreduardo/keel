// Must not compile: &Server::serve is a member of the base Loop (`int (Loop::*)()`), and no
// component provides Loop.
#include <keel.hpp>

struct Loop {
    int serve() { return 0; }
};

class Server : public keel::Component<>, public Loop {};

int main() {
    auto app = keel::compose<Server>();
    return app.run(&Server::serve);
}
