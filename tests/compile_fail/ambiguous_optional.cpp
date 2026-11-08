// Must not compile: keel::Optional<Storage> also needs at most one provider.
#include <keel.hpp>

struct Storage {
    virtual ~Storage() = default;
};

class DiskStorage : public keel::Component<>, public keel::Provides<Storage>, public Storage {};
class MemoryStorage : public keel::Component<>, public keel::Provides<Storage>, public Storage {};

class Service : public keel::Component<keel::Optional<Storage>> {};

static_assert(!keel::isValid<DiskStorage, Service, MemoryStorage>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<DiskStorage, Service, MemoryStorage>();
}
