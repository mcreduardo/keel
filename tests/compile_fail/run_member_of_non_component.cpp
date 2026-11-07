// Must not compile: run(&Cli::execute) calls execute() on the Cli component, and Cli isn't
// composed.
#include <keel.hpp>

class Logger : public keel::Component<> {};

class Cli : public keel::Component<Logger> {
public:
    int execute() { return 0; }
};

static_assert(keel::isValid<Logger>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Logger>();
    return app.run(&Cli::execute);
}
