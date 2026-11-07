// Must not compile: the Http bundle needs Logger, from a bundle that isn't composed.
#include <keel.hpp>

class Logger : public keel::Component<> {};
class Router : public keel::Component<Logger> {};
class HttpServer : public keel::Component<Router> {};

using Core = keel::Bundle<Logger>;
using Http = keel::Bundle<Router, HttpServer>;

static_assert(!keel::isValid<Http>, "isValid disagrees with compose");

int main() {
    keel::App<Http> app;
}
