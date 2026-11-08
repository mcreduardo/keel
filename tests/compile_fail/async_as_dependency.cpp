// Must not compile: keel::AsyncInit is listed in keel::Component<...>; it is a base class.
#include <keel.hpp>

class Config : public keel::Component<> {};

class Database : public keel::Component<Config, keel::AsyncInit> {};

static_assert(!keel::isValid<Config, Database>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Config, Database>();
}
