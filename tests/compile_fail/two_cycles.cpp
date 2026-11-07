// Must not compile: A and B form a cycle, and so do C and D. Middle sits between the two
// cycles without being on one, so the error must not name it.
#include <keel.hpp>

class A;
class B;
class C;
class D;
class Middle;
class A : public keel::Component<B> {};
class B : public keel::Component<A> {};
class Middle : public keel::Component<A> {};
class C : public keel::Component<Middle, D> {};
class D : public keel::Component<C> {};

static_assert(!keel::isValid<A, B, Middle, C, D>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<A, B, Middle, C, D>();
}
