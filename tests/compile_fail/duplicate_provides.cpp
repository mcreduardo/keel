// Must not compile: keel::Provides<Storage> is listed twice.
#include <keel.hpp>

struct Storage {
    virtual ~Storage() = default;
};

class DiskStorage : public keel::Component<keel::Provides<Storage>, keel::Provides<Storage>>, public Storage {};

class Service : public keel::Component<Storage> {};

int main() {
    auto app = keel::compose<Service, DiskStorage>();
}
