// Must not compile: keel::Optional<keel::All<keel::Optional<Plugin>>> could never match.
#include <keel.hpp>

struct Plugin {
    virtual ~Plugin() = default;
};

class PluginMenu : public keel::Component<keel::Optional<keel::All<keel::Optional<Plugin>>>> {};

int main() {
    auto app = keel::compose<PluginMenu>();
}
