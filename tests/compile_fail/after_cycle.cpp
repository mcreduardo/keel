// Must not compile: After orders like a dependency, so Worker -> After<Pool> ->
// Worker is a cycle.
#include <keel.hpp>

class Worker;
class Pool : public keel::Component<Worker> {};
class Worker : public keel::Component<keel::After<Pool>> {};

static_assert(!keel::isValid<Pool, Worker>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Pool, Worker>();
}
