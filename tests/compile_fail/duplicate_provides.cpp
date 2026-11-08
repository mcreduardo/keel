// Must not compile: keel::Provides<Storage> is listed twice in keel::Component<...>. It isn't
// a dependency (one error, not one per entry).
#include <keel.hpp>

struct Storage {
    virtual ~Storage() = default;
};

class DiskStorage : public keel::Component<keel::Provides<Storage>, keel::Provides<Storage>>, public Storage {};

class Service : public keel::Component<Storage> {};

static_assert(!keel::isValid<Service, DiskStorage>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Service, DiskStorage>();
}
