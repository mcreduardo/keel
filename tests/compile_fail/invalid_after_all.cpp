// Must not compile: keel::After<keel::All<const Plugin>> could never match.
#include <keel.hpp>

struct Plugin {
    virtual ~Plugin() = default;
};

class PluginMenu : public keel::Component<keel::After<keel::All<const Plugin>>> {};

static_assert(!keel::isValid<PluginMenu>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<PluginMenu>();
}
