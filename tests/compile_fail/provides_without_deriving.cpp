// Must not compile: DiskStorage lists keel::Provides<Storage> but doesn't derive from Storage.
#include <keel.hpp>

struct Storage {
    virtual ~Storage() = default;
};

class DiskStorage : public keel::Component<keel::Provides<Storage>> {};

class Service : public keel::Component<Storage> {};

static_assert(!keel::isValid<Service, DiskStorage>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Service, DiskStorage>();
    app.getComponent<Storage>();
}
