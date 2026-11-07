// Must not compile: keel::Provides<const Storage> could never match a dependency.
#include <keel.hpp>

struct Storage {
    virtual ~Storage() = default;
};

class DiskStorage : public keel::Component<keel::Provides<const Storage>>, public Storage {};

int main() {
    auto app = keel::compose<DiskStorage>();
}
