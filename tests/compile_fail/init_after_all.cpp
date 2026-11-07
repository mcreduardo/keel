// Must not compile: keel::InitAfter<keel::All<Plugin>> could never match; keel::InitAfter<Plugin>
// already orders after every provider.
#include <keel.hpp>

struct Plugin {
    virtual ~Plugin() = default;
};

class Spellcheck : public keel::Component<keel::Provides<Plugin>>, public Plugin {};

class PluginMenu : public keel::Component<keel::InitAfter<keel::All<Plugin>>> {};

static_assert(!keel::isValid<PluginMenu, Spellcheck>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<PluginMenu, Spellcheck>();
}
