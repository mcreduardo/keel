// bundles: named groups of components, several compositions in one program, and
// composition guards.
//
//   keel::Bundle<Cs...>          a group, listed in compose<...> / App<...> in place of Cs...
//   keel::requireValid<Cs...>    true, or the compile error compose<...> would give
//   keel::isValid<Cs...>         true or false, never a compile error
//
// The guards sit next to the compositions, so every variant is checked on every build,
// also the ones this binary doesn't run.
//
// Build: c++ -std=c++20 -I.. bundles.cpp -o bundles     (Linux: add -pthread)
// Run:   ./bundles            the production composition
//        ./bundles --bench    the same server on an in-memory database

#include "keel.hpp"

#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

class Config : public keel::Component<> {};

class Logger : public keel::Component<Config> {
public:
    void info(const std::string& message) const { std::printf("  [info] %s\n", message.c_str()); }
};

// Provides itself, so a subclass (InMemoryDatabase) can take its place.
class Database : public keel::Component<Config, Logger>, public keel::Provides<Database> {
public:
    virtual std::string name() const { return "postgres"; }

private:
    void init() override { getComponent<Logger>().info("connected to " + name()); }
};

class InMemoryDatabase : public Database {
public:
    std::string name() const override { return "in-memory"; }
};

class Migrations : public keel::Component<Database, Logger> {
    void init() override { getComponent<Logger>().info("schema up to date"); }
};

class Router : public keel::Component<Database, Logger> {};

class HttpServer : public keel::Component<Router, Logger, keel::After<Migrations>> {
public:
    int serve() {
        getComponent<Logger>().info("serving");
        return 0;
    }
};

// Groups, reused by the compositions below. A bundle may contain bundles.
using Core = keel::Bundle<Config, Logger>;
using Http = keel::Bundle<Router, HttpServer>;
using Persistence = keel::Bundle<Database, Migrations>;

using Production = keel::App<Core, Persistence, Http>;
using Bench = keel::App<Core, InMemoryDatabase, Migrations, Http>;

// Guards: true, or the named compile error (e.g. MissingDependency<Router, Database>).
static_assert(keel::requireValid<Core, Persistence, Http>);
static_assert(keel::requireValid<Core, InMemoryDatabase, Migrations, Http>);

// A composition that must not compile: Router needs a Database.
static_assert(!keel::isValid<Core, Http>);
// Two databases: the dependency on Database would be ambiguous.
static_assert(!keel::isValid<Core, Persistence, InMemoryDatabase, Http>);

int main(int argc, char** argv) try {
    if (argc > 1 && std::strcmp(argv[1], "--bench") == 0) {
        std::printf("== bench\n");
        return keel::run<Bench>(&HttpServer::serve);
    }
    std::printf("== production\n");
    return keel::run<Production>(&HttpServer::serve);
} catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
}
