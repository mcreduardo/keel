// Must not compile: Database lists keel::AsyncInit and looks it up. Listing it is the one
// error; the lookup adds none.
#include <keel.hpp>

class Config : public keel::Component<> {};

class Database : public keel::Component<Config, keel::AsyncInit> {
    void init() override { getComponent<keel::AsyncInit>(); }
};

static_assert(!keel::isValid<Config, Database>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Config, Database>();
}
