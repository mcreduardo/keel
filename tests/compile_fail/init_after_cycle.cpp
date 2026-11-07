// Must not compile: InitAfter orders like a dependency, so Worker -> InitAfter<Pool> ->
// Worker is a cycle.
#include <keel.hpp>

class Worker;
class Pool : public keel::Component<Worker> {};
class Worker : public keel::Component<keel::InitAfter<Pool>> {};

int main() {
    auto app = keel::compose<Pool, Worker>();
}
