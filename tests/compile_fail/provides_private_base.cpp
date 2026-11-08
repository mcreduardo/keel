// Must not compile: DiskStorage derives from keel::Provides<Storage>, but from Storage
// privately, so it can't be used as one.
#include <keel.hpp>

struct Storage {
    virtual ~Storage() = default;
};

class DiskStorage : public keel::Component<>, public keel::Provides<Storage>, Storage {};

class Service : public keel::Component<Storage> {};

static_assert(!keel::isValid<Service, DiskStorage>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Service, DiskStorage>();
}
