// Must not compile: A needs one Storage, and B and C provide it. C depends on A, which would
// also make a cycle through C if A were ordered after both; the ambiguity is the one error.
#include <keel.hpp>

struct Storage {
    virtual ~Storage() = default;
};

class A;
class B : public keel::Component<>, public keel::Provides<Storage>, public Storage {};
class C : public keel::Component<A>, public keel::Provides<Storage>, public Storage {};
class A : public keel::Component<Storage> {};

static_assert(!keel::isValid<A, B, C>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<A, B, C>();
}
