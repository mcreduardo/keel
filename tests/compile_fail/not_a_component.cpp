// Must not compile: Settings doesn't derive from keel::Component<...>.
#include <keel.hpp>

struct Settings {};
class Logger : public keel::Component<> {};

static_assert(!keel::isValid<Logger, Settings>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Logger, Settings>();
}
