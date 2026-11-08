// Must not compile: two components provide Storage, so the root can't get "the" Storage.
#include <keel.hpp>

struct Storage {
    virtual ~Storage() = default;
};

class DiskStorage : public keel::Component<>, public keel::Provides<Storage>, public Storage {};
class MemoryStorage : public keel::Component<>, public keel::Provides<Storage>, public Storage {};

static_assert(keel::isValid<DiskStorage, MemoryStorage>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<DiskStorage, MemoryStorage>();
    app.getComponent<Storage>();
}
