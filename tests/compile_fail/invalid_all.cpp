// Must not compile: keel::All<const Plugin> could never match a provider.
#include <keel.hpp>

struct Plugin {
    virtual ~Plugin() = default;
};

class Spellcheck : public keel::Component<keel::Provides<Plugin>>, public Plugin {};

class PluginMenu : public keel::Component<keel::All<const Plugin>> {};

int main() {
    auto app = keel::compose<PluginMenu, Spellcheck>();
}
