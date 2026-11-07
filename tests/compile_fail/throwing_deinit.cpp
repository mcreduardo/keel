// Must not compile: deinit() overrides a noexcept function, so it must be noexcept too.
#include <keel.hpp>

class Logger : public keel::Component<> {
    void deinit() override {}
};

int main() {
    auto app = keel::compose<Logger>();
}
