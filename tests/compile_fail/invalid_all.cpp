// Must not compile: keel::All<const Plugin> could never match a provider.
#include <keel.hpp>

struct Plugin {
    virtual ~Plugin() = default;
};

class Spellcheck : public keel::Component<>, public keel::Provides<Plugin>, public Plugin {};

class PluginMenu : public keel::Component<keel::All<const Plugin>> {};

static_assert(!keel::isValid<PluginMenu, Spellcheck>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<PluginMenu, Spellcheck>();
}
