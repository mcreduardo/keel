// Must not compile: keel::All<Plugin> and keel::Optional<keel::All<Plugin>> name the same
// dependency.
#include <keel.hpp>

struct Plugin {
    virtual ~Plugin() = default;
};

class Spellcheck : public keel::Component<keel::Provides<Plugin>>, public Plugin {};

class PluginMenu : public keel::Component<keel::All<Plugin>, keel::Optional<keel::All<Plugin>>> {};

static_assert(!keel::isValid<PluginMenu, Spellcheck>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<PluginMenu, Spellcheck>();
}
