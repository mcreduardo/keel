// Must not compile: run(&Shell::serve) needs the provider of Shell, and Terminal derives
// from keel::Provides<Shell> privately. Nothing else refers to Shell, so run() reports it.
#include <keel.hpp>

struct Shell {
    virtual ~Shell() = default;
    virtual int serve() = 0;
};

class Terminal : public keel::Component<>, keel::Provides<Shell>, public Shell {
public:
    int serve() override { return 0; }
};

static_assert(keel::isValid<Terminal>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Terminal>();
    return app.run(&Shell::serve);
}
