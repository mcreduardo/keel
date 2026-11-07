// Must not compile: DiskStorage derives from Storage twice (through Reader and Writer), so a
// Storage& to it would be ambiguous.
#include <keel.hpp>

struct Storage {
    virtual ~Storage() = default;
};
struct Reader : Storage {};
struct Writer : Storage {};

class DiskStorage : public keel::Component<keel::Provides<Storage>>, public Reader, public Writer {};

int main() {
    auto app = keel::compose<DiskStorage>();
}
