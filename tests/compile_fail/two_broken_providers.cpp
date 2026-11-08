// Must not compile, with two errors, one per mistaken provider of Storage: DiskStorage
// derives from keel::Provides<Storage> privately, and MemoryStorage doesn't derive from
// Storage. Service, which needs one Storage, isn't reported too.
#include <keel.hpp>

struct Storage {
    virtual ~Storage() = default;
};

class DiskStorage : public keel::Component<>, keel::Provides<Storage>, public Storage {};
class MemoryStorage : public keel::Component<>, public keel::Provides<Storage> {};

class Service : public keel::Component<Storage> {};

static_assert(!keel::isValid<Service, DiskStorage, MemoryStorage>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Service, DiskStorage, MemoryStorage>();
}
