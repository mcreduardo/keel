// Must not compile: keel::AsyncInit is a base class of a component, not something to list
// in compose<...>.
#include <keel.hpp>

class Database : public keel::Component<> {};

static_assert(!keel::isValid<Database, keel::AsyncInit>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Database, keel::AsyncInit>();
}
