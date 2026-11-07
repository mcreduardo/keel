// Must not compile: keel::AsyncInit is a marker, not a dependency to look up.
#include <keel.hpp>

class Database : public keel::Component<keel::AsyncInit> {
    void init() override { getComponent<keel::AsyncInit>(); }
};

static_assert(keel::isValid<Database>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Database>();
}
