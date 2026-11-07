// Must not compile: the generic lambda's body is wrong. That is the one error; keel doesn't
// add "not callable".
#include <keel.hpp>

class Cli : public keel::Component<> {};

int main() {
    return keel::run<keel::App<Cli>>([](auto& running) { return running.nope(); });
}
