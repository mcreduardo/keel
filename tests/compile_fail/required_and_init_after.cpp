// Must not compile: Server lists Logger both as required and as InitAfter. The
// getComponent<Logger>() call is fine; the duplicate is the one error.
#include <keel.hpp>

class Logger : public keel::Component<> {};

class Server : public keel::Component<Logger, keel::InitAfter<Logger>> {
    void init() override { getComponent<Logger>(); }
};

static_assert(!keel::isValid<Logger, Server>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Logger, Server>();
    app.init();
}
