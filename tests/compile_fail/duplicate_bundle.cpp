// Must not compile: the Core bundle is listed, and is also inside Http. It is one error,
// not one per component of Core.
#include <keel.hpp>

class Config : public keel::Component<> {};
class Logger : public keel::Component<Config> {};
class Router : public keel::Component<Logger> {};
using Core = keel::Bundle<Config, Logger>;
using Http = keel::Bundle<Core, Router>;

static_assert(!keel::isValid<Core, Http>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Core, Http>();
}
