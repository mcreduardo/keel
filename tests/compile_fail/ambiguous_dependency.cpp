// Must not compile: Service needs one Storage, and two components provide it.
#include <keel.hpp>

struct Storage {
    virtual ~Storage() = default;
};

class DiskStorage : public keel::Component<>, public keel::Provides<Storage>, public Storage {};
class MemoryStorage : public keel::Component<>, public keel::Provides<Storage>, public Storage {};

class Service : public keel::Component<Storage> {};

static_assert(!keel::isValid<Service, DiskStorage, MemoryStorage>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Service, DiskStorage, MemoryStorage>();
}
