// lifecycle: init/start/stop/deinit, ordering-only dependencies, the ways to drive an
// App, and what happens when startup fails.
//
//   construct (dependency order) -> init() -> start() -> ... -> stop() -> deinit()
//   -> destroy; stop(), deinit() and destruction in reverse order.
//
// Build: c++ -std=c++20 -I.. lifecycle.cpp -o lifecycle     (Linux: add -pthread)
// Run:   ./lifecycle            the three ways to drive an App
//        ./lifecycle --fail     the HttpServer's start() throws: watch the rollback

#include "keel.hpp"

#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string_view>

// Prints "<what> <component>", e.g. "init   Logger".
void say(std::string_view what, std::string_view component) {
    std::printf("  %-6.*s %.*s\n", static_cast<int>(what.size()), what.data(), static_cast<int>(component.size()),
                component.data());
}

class Options : public keel::Component<> {
public:
    Options(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) failStart = failStart || std::strcmp(argv[i], "--fail") == 0;
    }
    bool failStart = false;
};

class Logger : public keel::Component<> {
    void init() override { say("init", componentName()); }
    void deinit() noexcept override { say("deinit", componentName()); }
};

class Database : public keel::Component<Logger> {
    void init() override { say("init", componentName()); }      // connect
    void start() override { say("start", componentName()); }    // accept queries
    void stop() noexcept override { say("stop", componentName()); }
    void deinit() noexcept override { say("deinit", componentName()); }  // disconnect
};

// Runs the schema migrations in init(). Nobody calls it, but the server must not start
// serving before it has run.
class Migrations : public keel::Component<Database> {
    void init() override { say("init", componentName()); }
};

// keel::After<Migrations>: ordering only (no access to Migrations).
class HttpServer : public keel::Component<Options, Database, Logger, keel::After<Migrations>> {
public:
    int requests() const { return 3; }

private:
    void init() override { say("init", componentName()); }  // bind the socket
    void start() override {
        if (getComponent<Options>().failStart) throw std::runtime_error("cannot listen on port 8080");
        say("start", componentName());  // accept connections once everything else is up
    }
    void stop() noexcept override { say("stop", componentName()); }  // stop accepting, before the Database stops
    void deinit() noexcept override { say("deinit", componentName()); }
};

using Server = keel::App<HttpServer, Migrations, Database, Logger, Options>;

int main(int argc, char** argv) try {
    std::printf("== run(fn): init, start, fn, stop, deinit\n");
    {
        Server app(argc, argv);
        const int served = app.run([](Server& running) {
            std::printf("  ... running\n");
            return running.getComponent<HttpServer>().requests();
        });
        std::printf("  run returned %d\n", served);
    }

    std::printf("== keel::run with a member function\n");
    const int served = keel::run<Server>(&HttpServer::requests, argc, argv);
    std::printf("  run returned %d\n", served);

    std::printf("== step by step\n");
    {
        Server app(argc, argv);
        app.init();
        app.start();
        std::printf("  ... running\n");
        app.stop();
        app.deinit();  // the destructor would do it too
    }
} catch (const std::exception& e) {
    // A failed start(): the components already started are stopped, every component is
    // deinitialized, both in reverse order, and the exception arrives here unchanged.
    std::printf("startup failed: %s\n", e.what());
    return 1;
}
