// Must not compile: a composed optional dependency orders like a required one, so
// Cache -> Optional<Store> -> Cache is a cycle.
#include <keel.hpp>

class Cache;
class Store : public keel::Component<Cache> {};
class Cache : public keel::Component<keel::Optional<Store>> {};

int main() {
    auto app = keel::compose<Store, Cache>();
}
