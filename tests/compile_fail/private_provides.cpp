// Must not compile: DiskStorage derives from keel::Provides<Storage> privately (the default
// for a class), so keel can't see that it provides Storage. Reported once, though two
// components depend on Storage, and not as a missing dependency too.
#include <keel.hpp>

struct Storage {
    virtual ~Storage() = default;
};

class DiskStorage : public keel::Component<>, keel::Provides<Storage>, public Storage {};

class Service : public keel::Component<Storage> {};
class Report : public keel::Component<keel::Optional<Storage>> {};

static_assert(!keel::isValid<Report, Service, DiskStorage>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Report, Service, DiskStorage>();
    app.getComponent<Storage>();
}
