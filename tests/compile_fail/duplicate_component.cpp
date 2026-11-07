// Must not compile: Logger is listed twice (three times, still one error).
#include <keel.hpp>

class Logger : public keel::Component<> {};

static_assert(!keel::isValid<Logger, Logger, Logger>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Logger, Logger, Logger>();
}
