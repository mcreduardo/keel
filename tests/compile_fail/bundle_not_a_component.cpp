// Must not compile: a nested bundle contains Settings, which isn't a component.
#include <keel.hpp>

struct Settings {};
class Logger : public keel::Component<> {};

using Core = keel::Bundle<Logger, keel::Bundle<Settings>>;

static_assert(!keel::isValid<Core>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Core>();
}
