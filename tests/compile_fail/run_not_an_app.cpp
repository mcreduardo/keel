// Must not compile: keel::run<AppType> needs a keel::App<...>, and Cli is a component.
#include <keel.hpp>

class Cli : public keel::Component<> {};

int main() {
    return keel::run<Cli>([] { return 0; });
}
