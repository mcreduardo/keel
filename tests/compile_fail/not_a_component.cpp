// Must not compile: Settings doesn't derive from keel::Component<...>.
#include <keel.hpp>

struct Settings {};
class Logger : public keel::Component<> {};

int main() {
    auto app = keel::compose<Logger, Settings>();
}
