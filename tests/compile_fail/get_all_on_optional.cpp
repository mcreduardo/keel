// Must not compile: Storage is declared keel::Optional<Storage>; use findComponent.
#include <keel.hpp>

struct Storage {
    virtual ~Storage() = default;
};

class Service : public keel::Component<keel::Optional<Storage>> {
    void init() override { getAll<Storage>(); }
};

int main() {}
