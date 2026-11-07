// Must not compile: Repository comes after every alternative of Cache, and MemoryCache
// needs Repository.
#include <keel.hpp>

struct Cache {
    virtual ~Cache() = default;
};

class Repository : public keel::Component<Cache> {};
class RedisCache : public keel::Component<keel::Alternative<Cache>>, public Cache {};
class MemoryCache : public keel::Component<Repository, keel::Alternative<Cache>>, public Cache {};

int main() {
    auto app = keel::compose<Repository, RedisCache, MemoryCache>();
}
