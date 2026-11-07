// Must not compile: keel::AsyncInit is a marker, not a dependency; keel::Optional<> of it
// means nothing.
#include <keel.hpp>

class Config : public keel::Component<> {};

class Database : public keel::Component<Config, keel::Optional<keel::AsyncInit>> {};

static_assert(!keel::isValid<Config, Database>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Config, Database>();
}
