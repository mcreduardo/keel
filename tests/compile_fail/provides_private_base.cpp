// Must not compile: DiskStorage derives from Storage privately, so it can't be used as one.
#include <keel.hpp>

struct Storage {
    virtual ~Storage() = default;
};

class DiskStorage : public keel::Component<keel::Provides<Storage>>, Storage {};

static_assert(!keel::isValid<DiskStorage>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<DiskStorage>();
}
