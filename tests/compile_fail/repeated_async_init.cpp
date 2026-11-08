// Must not compile: FastDatabase derives from keel::AsyncInit twice: itself, and through
// Database, which is async already.
#include <keel.hpp>

class Database : public keel::Component<>, public keel::AsyncInit {};
class FastDatabase : public Database, public keel::AsyncInit {};

static_assert(!keel::isValid<FastDatabase>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<FastDatabase>();
}
