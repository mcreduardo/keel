// Must not compile: Sink is abstract. Compose a concrete implementation instead.
#include <keel.hpp>

class Sink : public keel::Component<> {
public:
    virtual void write() = 0;
};

int main() {
    auto app = keel::compose<Sink>();
}
