// Must not compile: keel::AsyncInit goes in a component's keel::Component<...> list, not in
// compose<...>.
#include <keel.hpp>

class Database : public keel::Component<> {};

static_assert(!keel::isValid<Database, keel::AsyncInit>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Database, keel::AsyncInit>();
}
