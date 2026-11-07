// Must not compile: P and Q depend on each other. initOrder(), toDot() and timings() on the
// App add no error of their own (the init order is incomplete).
#include <keel.hpp>

#include <string>

class P;
class Q;
class P : public keel::Component<Q> {};
class Q : public keel::Component<P> {};
class Logger : public keel::Component<> {};

static_assert(!keel::isValid<P, Q, Logger>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<P, Q, Logger>();
    constexpr auto order = decltype(app)::initOrder();
    const std::string dot = app.toDot();
    return static_cast<int>(order.size() + dot.size() + app.timings().size());
}
