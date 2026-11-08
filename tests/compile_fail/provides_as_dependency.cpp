// Must not compile: keel::Provides<Storage> is listed in keel::Component<...>; it is a base
// class. Service, which depends on Storage, isn't reported too.
#include <keel.hpp>

struct Storage {
    virtual ~Storage() = default;
};

class DiskStorage : public keel::Component<keel::Provides<Storage>>, public Storage {};

class Service : public keel::Component<Storage> {};

static_assert(!keel::isValid<Service, DiskStorage>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Service, DiskStorage>();
}
