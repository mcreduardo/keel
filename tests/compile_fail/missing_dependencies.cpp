// Must not compile: two components each miss a dependency. Both are reported, one error
// each (COUNT_ERRORS doesn't apply: two mistakes, two errors).
#include <keel.hpp>

class Logger : public keel::Component<> {};
class Config : public keel::Component<> {};
class Server : public keel::Component<Logger> {};
class Exporter : public keel::Component<Config> {};

static_assert(!keel::isValid<Server, Exporter>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Server, Exporter>();
}
