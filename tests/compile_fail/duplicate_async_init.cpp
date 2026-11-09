// Must not compile: Database lists keel::AsyncInit twice (one InvalidDependency, as for once).
#include <keel.hpp>

class Config : public keel::Component<> {};

class Database : public keel::Component<Config, keel::AsyncInit, keel::AsyncInit> {};

static_assert(!keel::isValid<Config, Database>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Config, Database>();
}
