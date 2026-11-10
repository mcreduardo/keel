// Must not compile: Spellcheck derives from Plugin without keel::Provides<Plugin>, so
// keel::After<keel::All<Plugin>> would silently not wait for it.
#include <keel.hpp>

struct Plugin {
    virtual ~Plugin() = default;
};

class Spellcheck : public keel::Component<>, public Plugin {};

class PluginMenu : public keel::Component<keel::After<keel::All<Plugin>>> {};

static_assert(!keel::isValid<PluginMenu, Spellcheck>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<PluginMenu, Spellcheck>();
}
