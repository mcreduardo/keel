// Must not compile: run(&Shell::serve) needs one Shell, and two components provide it.
#include <keel.hpp>

struct Shell {
    virtual ~Shell() = default;
    virtual int serve() = 0;
};

class Terminal : public keel::Component<keel::Provides<Shell>>, public Shell {
    int serve() override { return 0; }
};
class Console : public keel::Component<keel::Provides<Shell>>, public Shell {
    int serve() override { return 0; }
};

static_assert(keel::isValid<Terminal, Console>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Terminal, Console>();
    return app.run(&Shell::serve);
}
