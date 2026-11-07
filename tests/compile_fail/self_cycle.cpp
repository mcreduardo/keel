// Must not compile: C provides I and depends on I, which resolves to C itself. The cycle
// is C alone, and Client, which only depends on it, isn't named.
#include <keel.hpp>

struct I {
    virtual ~I() = default;
};
class C : public keel::Component<I, keel::Provides<I>>, public I {};
class Client : public keel::Component<C> {};

static_assert(!keel::isValid<Client, C>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Client, C>();
}
