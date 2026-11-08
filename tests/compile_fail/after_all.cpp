// Must not compile: keel::After<keel::All<Plugin>> could never match; keel::After<Plugin>
// already orders after every provider.
#include <keel.hpp>

struct Plugin {
    virtual ~Plugin() = default;
};

class Spellcheck : public keel::Component<>, public keel::Provides<Plugin>, public Plugin {};

class PluginMenu : public keel::Component<keel::After<keel::All<Plugin>>> {};

static_assert(!keel::isValid<PluginMenu, Spellcheck>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<PluginMenu, Spellcheck>();
}
