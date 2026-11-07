// Composes two components from an installed keel.hpp.
#include <keel.hpp>

struct Config : keel::Component<> {
    int port = 8080;
};

struct Server : keel::Component<Config> {
    int port = 0;
    void init() override { port = getComponent<Config>().port; }
};

int main() {
    auto app = keel::compose<Server, Config>();
    app.init();
    const int port = app.getComponent<Server>().port;
    app.deinit();
    return port == 8080 ? 0 : 1;
}
