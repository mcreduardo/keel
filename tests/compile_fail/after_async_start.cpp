// Must not compile: keel::AsyncStart is a marker, not something to order after.
#include <keel.hpp>

class Config : public keel::Component<> {};

class Feed : public keel::Component<Config, keel::After<keel::AsyncStart>> {};

static_assert(!keel::isValid<Config, Feed>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Config, Feed>();
}
