// Must not compile: Plugin and keel::After<keel::All<Plugin>> name the same dependency.
#include <keel.hpp>

struct Plugin {
    virtual ~Plugin() = default;
};

class Spellcheck : public keel::Component<>, public keel::Provides<Plugin>, public Plugin {};

class PluginMenu : public keel::Component<Plugin, keel::After<keel::All<Plugin>>> {};

static_assert(!keel::isValid<PluginMenu, Spellcheck>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<PluginMenu, Spellcheck>();
}
