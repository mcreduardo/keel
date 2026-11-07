// Must not compile: Plugin is declared keel::Optional<keel::All<Plugin>>; use getAll.
#include <keel.hpp>

struct Plugin {
    virtual ~Plugin() = default;
};

class PluginMenu : public keel::Component<keel::Optional<keel::All<Plugin>>> {
    void init() override { findComponent<Plugin>(); }
};

int main() {}
