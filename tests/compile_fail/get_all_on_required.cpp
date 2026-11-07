// Must not compile: Storage is a required dependency (one provider); use getComponent.
#include <keel.hpp>

struct Storage {
    virtual ~Storage() = default;
};

class Service : public keel::Component<Storage> {
    void init() override { getAll<Storage>(); }
};

int main() {}
