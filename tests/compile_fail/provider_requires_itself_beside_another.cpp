// Must not compile: Wrapper provides Storage and requires Storage, which names Wrapper too:
// a cycle, not an ambiguity, though DiskStorage provides it as well.
#include <keel.hpp>

struct Storage {
    virtual ~Storage() = default;
};

class DiskStorage : public keel::Component<>, public keel::Provides<Storage>, public Storage {};
class Wrapper : public keel::Component<Storage>, public keel::Provides<Storage>, public Storage {};

static_assert(!keel::isValid<Wrapper, DiskStorage>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Wrapper, DiskStorage>();
}
