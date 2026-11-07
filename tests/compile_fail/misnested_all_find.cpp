// Must not compile: keel::All<keel::Optional<Plugin>> is written keel::Optional<keel::All<Plugin>>.
// The findComponent call adds no second error.
#include <keel.hpp>

struct Plugin {
    virtual ~Plugin() = default;
};

class PluginMenu : public keel::Component<keel::All<keel::Optional<Plugin>>> {
    void init() override { findComponent<Plugin>(); }
};

static_assert(!keel::isValid<PluginMenu>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<PluginMenu>();
}
