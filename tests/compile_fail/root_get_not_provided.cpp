// Must not compile: Storage is not a component of the App, and nothing provides it.
#include <keel.hpp>

struct Storage {
    virtual ~Storage() = default;
};

class DiskStorage : public keel::Component<>, public Storage {};

static_assert(keel::isValid<DiskStorage>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<DiskStorage>();
    app.getComponent<Storage>();
}
