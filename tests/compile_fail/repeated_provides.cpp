// Must not compile: Disk derives from keel::Provides<Storage> twice: itself, and through
// Device, which already provides Storage. keel can't use it as a provider.
#include <keel.hpp>

struct Storage {
    virtual ~Storage() = default;
};

class Device : public keel::Component<>, public keel::Provides<Storage>, public Storage {};
class Disk : public Device, public keel::Provides<Storage> {};

class Service : public keel::Component<Storage> {};

static_assert(!keel::isValid<Service, Disk>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Service, Disk>();
}
