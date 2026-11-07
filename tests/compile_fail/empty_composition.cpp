// Must not compile: the only bundle is empty, so there is nothing to compose.
#include <keel.hpp>

using Extras = keel::Bundle<>;

static_assert(!keel::isValid<Extras>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Extras>();
}
