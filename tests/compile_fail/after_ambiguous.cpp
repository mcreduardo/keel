// Must not compile: keel::After<Storage> names one Storage, and two components provide it.
// keel::After<keel::All<Storage>> would come after both.
#include <keel.hpp>

struct Storage {
    virtual ~Storage() = default;
};

class DiskStorage : public keel::Component<>, public keel::Provides<Storage>, public Storage {};
class MemoryStorage : public keel::Component<>, public keel::Provides<Storage>, public Storage {};

class Backup : public keel::Component<keel::After<Storage>> {};

static_assert(!keel::isValid<Backup, DiskStorage, MemoryStorage>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Backup, DiskStorage, MemoryStorage>();
}
