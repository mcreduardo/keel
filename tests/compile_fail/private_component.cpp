// Must not compile: Hidden derives from keel::Component<> privately.
#include <keel.hpp>

class Hidden : keel::Component<> {};

static_assert(!keel::isValid<Hidden>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Hidden>();
}
