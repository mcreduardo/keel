// Must not compile: run(&Cli::execute) calls execute() on an lvalue Cli, and it is
// &&-qualified.
#include <keel.hpp>

class Cli : public keel::Component<> {
public:
    int execute() && { return 0; }
};

int main() {
    auto app = keel::compose<Cli>();
    return app.run(&Cli::execute) == 0 ? 0 : 1;
}
