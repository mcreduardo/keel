// Must not compile: Storage is declared keel::After<Storage>, which is ordering only.
#include <keel.hpp>

struct Storage {
    virtual ~Storage() = default;
};

class Backup : public keel::Component<keel::After<Storage>> {
    void init() override { getAll<Storage>(); }
};

int main() {}
