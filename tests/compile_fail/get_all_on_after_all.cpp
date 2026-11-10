// Must not compile: keel::After<keel::All<Plugin>> is ordering only, so PluginMenu can't
// use the plugins.
#include <keel.hpp>

struct Plugin {
    virtual ~Plugin() = default;
};

class Spellcheck : public keel::Component<>, public keel::Provides<Plugin>, public Plugin {};

class PluginMenu : public keel::Component<keel::After<keel::All<Plugin>>> {
    void init() override { getAll<Plugin>(); }
};

static_assert(keel::isValid<PluginMenu, Spellcheck>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<PluginMenu, Spellcheck>();
    app.init();
}
