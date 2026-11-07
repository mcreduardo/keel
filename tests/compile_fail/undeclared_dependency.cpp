// Must not compile: Server uses Logger without listing it in keel::Component<...>.
#include <keel.hpp>

class Logger : public keel::Component<> {};

class Server : public keel::Component<> {
    void init() override { getComponent<Logger>(); }
};

static_assert(keel::isValid<Logger, Server>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Logger, Server>();
    app.init();
}
