// Must not compile: P, Q and R depend on each other in a cycle. The error names only
// them, not Outside, which merely depends on the cycle.
#include <keel.hpp>

class P;
class Q;
class R;
class P : public keel::Component<R> {};
class Q : public keel::Component<P> {};
class R : public keel::Component<Q> {};
class Outside : public keel::Component<P> {};

int main() {
    auto app = keel::compose<Outside, P, Q, R>();
}
