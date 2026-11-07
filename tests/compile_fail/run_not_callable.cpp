// Must not compile: run(fn) calls fn with no arguments or with the App, and this one takes
// an int.
#include <keel.hpp>

class Cli : public keel::Component<> {};

static_assert(keel::isValid<Cli>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Cli>();
    return app.run([](int) { return 0; });
}
