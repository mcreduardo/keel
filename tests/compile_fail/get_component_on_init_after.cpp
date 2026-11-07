// Must not compile: InitAfter<Migrations> is ordering only, so Server can't use Migrations.
#include <keel.hpp>

class Migrations : public keel::Component<> {};

class Server : public keel::Component<keel::InitAfter<Migrations>> {
    void init() override { getComponent<Migrations>(); }
};

int main() {
    auto app = keel::compose<Migrations, Server>();
    app.init();
}
