// Must not compile: three accessor mistakes in a component whose list also has an invalid
// entry (keel::Provides<Storage>). The entry silences only lookups of Storage, so each
// accessor mistake is still its own error (here without composing, so the entry itself
// isn't reported).
#include <keel.hpp>

struct Storage {
    virtual ~Storage() = default;
};
class Config : public keel::Component<> {};
class Logger : public keel::Component<Config> {};

class Server : public keel::Component<Config, keel::Provides<Storage>>, public Storage {
public:
    void init() override;
};

void Server::init() {
    getComponent<Storage>();  // the list's mistake: no second error
    getComponent<Logger>();
    findComponent<Config>();
    getAll<Config>();
}

int main() {}
