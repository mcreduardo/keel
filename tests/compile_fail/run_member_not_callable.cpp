// Must not compile: run(&Cli::execute) calls execute() with no arguments, and it takes one.
#include <keel.hpp>

class Cli : public keel::Component<> {
public:
    int execute(int verbosity) { return verbosity; }
};

static_assert(keel::isValid<Cli>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Cli>();
    return app.run(&Cli::execute) + 1;  // the result in an expression adds no error
}
