// Must not compile: Feed derives from keel::AsyncStart privately (the default for a class),
// so keel can't see it.
#include <keel.hpp>

class Config : public keel::Component<> {};

class Feed : public keel::Component<Config>, keel::AsyncStart {};

static_assert(!keel::isValid<Config, Feed>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Config, Feed>();
}
