// Must not compile: Hidden derives from keel::Component<> privately.
#include <keel.hpp>

class Hidden : keel::Component<> {};

int main() {
    auto app = keel::compose<Hidden>();
}
