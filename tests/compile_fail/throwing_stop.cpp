// Must not compile: stop() overrides a noexcept function, so it must be noexcept too.
#include <keel.hpp>

class Server : public keel::Component<> {
    void stop() override {}
};

int main() {
    auto app = keel::compose<Server>();
}
