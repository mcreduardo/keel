// Must not compile: run(&Shell::serve) calls serve() on the one provider of Shell, which
// configuration may turn off.
#include <keel.hpp>

struct Shell {
    virtual ~Shell() = default;
    virtual int serve() = 0;
};

class Terminal : public keel::Component<>, public keel::Provides<Shell>, public Shell, public keel::Conditional {
public:
    int serve() override { return 0; }

private:
    bool enabled() const override { return true; }
};

static_assert(keel::isValid<Terminal>, "isValid disagrees with compose");

int main() {
    keel::App<Terminal> app;
    return app.run(&Shell::serve);
}
