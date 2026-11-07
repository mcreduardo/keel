// Must not compile: both bundles contain Logger, so the flattened list has it twice.
#include <keel.hpp>

class Config : public keel::Component<> {};
class Logger : public keel::Component<Config> {};
class Database : public keel::Component<Logger> {};
class HttpServer : public keel::Component<Logger> {};

using Storage = keel::Bundle<Config, Logger, Database>;
using Http = keel::Bundle<Logger, HttpServer>;

static_assert(!keel::isValid<Storage, Http>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Storage, Http>();
}
