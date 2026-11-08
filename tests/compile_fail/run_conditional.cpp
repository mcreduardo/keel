// Must not compile: run(&Uplink::send) calls send() on Uplink, which configuration may
// turn off.
#include <keel.hpp>

class Uplink : public keel::Component<>, public keel::Conditional {
public:
    int send() { return 0; }

private:
    bool enabled() const override { return true; }
};

static_assert(keel::isValid<Uplink>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Uplink>();
    return app.run(&Uplink::send);
}
