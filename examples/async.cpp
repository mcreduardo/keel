// async: slow init() and start() on threads of their own.
//
// Database and Cache take ~100 ms to connect and list keel::AsyncInit: each runs its
// init() on its own thread as soon as its dependencies are done. Meanwhile the main
// thread initializes what doesn't wait for them (AuditLog), then comes back for Metrics
// and HttpServer. Feed lists keel::AsyncStart: its start() (subscribing) runs on its own
// thread too.
//
// Once any component is async, components with no dependency path between them may run
// at the same time, sync ones included: the Logger they all share is thread-safe.
//
// Build: c++ -std=c++20 -I.. async.cpp -o async     (Linux: add -pthread)
// Run:   ./async            Database and Cache connect at the same time
//        ./async --sync     everything on the main thread, in init order (about 100 ms slower)

#include "keel.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <mutex>
#include <string_view>
#include <thread>

static const std::thread::id mainThread = std::this_thread::get_id();

// Shared by every component, so it must be thread-safe.
class Logger : public keel::Component<> {
public:
    // Simulates `ms` of work, then prints which thread did it.
    void step(std::string_view component, std::string_view what, int ms = 0) const {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        const std::lock_guard lock(mutex_);
        const char* thread = std::this_thread::get_id() == mainThread ? "main" : "async";
        std::printf("  %-6.*s %-10.*s (%s thread)\n", static_cast<int>(what.size()), what.data(),
                    static_cast<int>(component.size()), component.data(), thread);
    }

private:
    mutable std::mutex mutex_;
};

class Config : public keel::Component<Logger> {
    void init() override { getComponent<Logger>().step(componentName(), "init", 20); }
};

// Slow to connect. They don't depend on each other, so they connect at the same time.
class Database : public keel::Component<Config, Logger, keel::AsyncInit> {
    void init() override { getComponent<Logger>().step(componentName(), "init", 100); }
};

class Cache : public keel::Component<Config, Logger, keel::AsyncInit> {
    void init() override { getComponent<Logger>().step(componentName(), "init", 100); }
};

// Doesn't wait for anything async: the main thread runs it while they connect.
class AuditLog : public keel::Component<Logger> {
    void init() override { getComponent<Logger>().step(componentName(), "init", 20); }
};

// Reports the Database's pool size, so it comes after it (ordering only).
class Metrics : public keel::Component<Logger, keel::InitAfter<Database>> {
    void init() override { getComponent<Logger>().step(componentName(), "init", 20); }
};

// Subscribing takes a while: start() on its own thread. start() must return; work that
// runs until stop() belongs on a thread the component starts here and joins in stop().
class Feed : public keel::Component<Logger, keel::AsyncStart> {
    void start() override { getComponent<Logger>().step(componentName(), "start", 50); }
    void stop() noexcept override { getComponent<Logger>().step(componentName(), "stop"); }
};

class HttpServer : public keel::Component<Database, Cache, Logger, keel::Optional<Metrics>> {
    void init() override { getComponent<Logger>().step(componentName(), "init", 20); }
    void start() override { getComponent<Logger>().step(componentName(), "start"); }
    void stop() noexcept override { getComponent<Logger>().step(componentName(), "stop"); }
};

using Server = keel::App<HttpServer, Metrics, Database, Cache, Config, AuditLog, Feed, Logger>;

int main(int argc, char** argv) try {
    Server app;
    if (argc > 1 && std::strcmp(argv[1], "--sync") == 0) app.setAsync(false);  // before init()

    const auto begin = std::chrono::steady_clock::now();
    std::printf("init:\n");
    app.init();  // returns when every init() has finished, async ones included
    const auto initDone = std::chrono::steady_clock::now();
    std::printf("start:\n");
    app.start();
    std::printf("stop:\n");
    app.stop();  // always on this thread, in reverse order
    app.deinit();

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(initDone - begin);
    std::printf("init took %lld ms\n", static_cast<long long>(ms.count()));
} catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
}
