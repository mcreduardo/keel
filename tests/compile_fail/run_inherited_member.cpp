// Must not compile: &Server::serve is a member of the base Loop (`int (Loop::*)()`), and no
// component provides Loop.
#include <keel.hpp>

struct Loop {
    int serve() { return 0; }
};

class Server : public keel::Component<>, public Loop {};

static_assert(keel::isValid<Server>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Server>();
    return app.run(&Server::serve);
}
