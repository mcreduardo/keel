// Must not compile: keel::Provides<const Storage> could never match a dependency. It is
// rejected where the component is constructed; isValid, which sees no use of it, can't
// tell.
#include <keel.hpp>

struct Storage {
    virtual ~Storage() = default;
};

class DiskStorage : public keel::Component<>, public keel::Provides<const Storage>, public Storage {};

static_assert(keel::isValid<DiskStorage>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<DiskStorage>();
}
