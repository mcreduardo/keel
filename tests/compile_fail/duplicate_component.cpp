// Must not compile: Logger is listed twice (three times, still one error).
#include <keel.hpp>

class Logger : public keel::Component<> {};

int main() {
    auto app = keel::compose<Logger, Logger, Logger>();
}
