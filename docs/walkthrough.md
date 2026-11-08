# A whole application

[keel](../README.md) › docs › A whole application

One small service that uses the main features together: configuration from the command
line, an optional component, an ordering-only dependency, an implementation chosen at
runtime, a composition guard, and the init order and timings. The code blocks on this page,
in order, are one program.

```sh
./service                 # in-memory storage, metrics on
./service db --no-metrics # storage in the database, no metrics
```

## Configuration

A settings struct, filled from the command line. `Config` is the only component that sees
`argc`/`argv`; the others ask `Config`.

```cpp
#include "keel.hpp"

#include <chrono>
#include <cstdio>
#include <exception>
#include <map>
#include <string>
#include <string_view>

struct Settings {
    std::string storage = "memory";  // or "db"
    bool metrics = true;
    int port = 8080;
};

class Config : public keel::Component<> {
public:
    Config(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--no-metrics") settings_.metrics = false;
            else settings_.storage = arg;
        }
    }
    const Settings& settings() const { return settings_; }

private:
    Settings settings_;
};

class Logger : public keel::Component<Config> {
public:
    void info(const std::string& message) const { std::printf("[info] %s\n", message.c_str()); }

private:
    void init() override { info("logger up"); }
    void deinit() noexcept override { info("logger down"); }  // the last to go
};
```

## An optional component

`Metrics` may be turned off by configuration (`keel::Conditional`), and `Database` works
without it (`keel::Optional`; a plain `Metrics` in its list wouldn't compile). See
[Decisions at runtime](runtime-decisions.md) and
[Dependencies](dependencies.md#optional-keeloptionalt).

```cpp
class Metrics : public keel::Component<Config>, public keel::Conditional {
public:
    void count() { ++count_; }
    int total() const { return count_; }

private:
    bool enabled() const override { return getComponent<Config>().settings().metrics; }
    int count_ = 0;
};

class Database : public keel::Component<Logger, keel::Optional<Metrics>> {
public:
    void query(const std::string& sql) {
        if (metrics_ != nullptr) metrics_->count();
        logger_->info("db: " + sql);
    }

private:
    void init() override {
        logger_ = &getComponent<Logger>();
        metrics_ = findComponent<Metrics>();  // nullptr: not composed, or disabled
        logger_->info(metrics_ != nullptr ? "db connected (with metrics)" : "db connected");
    }
    void deinit() noexcept override { logger_->info("db disconnected"); }

    const Logger* logger_ = nullptr;
    Metrics* metrics_ = nullptr;
};

// Nobody calls it; the server must only start after it ran (keel::InitAfter below).
class Migrations : public keel::Component<Database> {
    void init() override { getComponent<Database>().query("migrate schema"); }
};
```

## An implementation chosen at runtime

Two implementations of `Storage`, both composed; configuration enables one
(`keel::Alternative` and `keel::Conditional`). They are components of their own because
they need different dependencies: only `DatabaseStorage` uses the `Database`. (When the
implementations need the same ones, one component that creates the configured
implementation is simpler: see [Decisions at runtime](runtime-decisions.md).)
Dependents just use `Storage`.

```cpp
struct Storage {
    virtual ~Storage() = default;
    virtual void put(const std::string& key, const std::string& value) = 0;
};

class MemoryStorage : public keel::Component<Config, keel::Alternative<Storage>>, public Storage,
                      public keel::Conditional {
public:
    void put(const std::string& key, const std::string& value) override { data_[key] = value; }

private:
    bool enabled() const override { return getComponent<Config>().settings().storage == "memory"; }
    std::map<std::string, std::string> data_;
};

class DatabaseStorage : public keel::Component<Config, Database, keel::Alternative<Storage>>, public Storage,
                        public keel::Conditional {
public:
    void put(const std::string& key, const std::string& value) override { database_->query("put " + key + "=" + value); }

private:
    bool enabled() const override { return getComponent<Config>().settings().storage == "db"; }
    void init() override { database_ = &getComponent<Database>(); }
    Database* database_ = nullptr;
};
```

## The top of the graph

`HttpServer` uses the chosen `Storage`, and comes after `Migrations` without using it
(`keel::InitAfter`). Its `serve()` is what `main()` runs; `stop()` runs after it returns.

```cpp
class HttpServer : public keel::Component<Config, Logger, Storage, keel::InitAfter<Migrations>> {
public:
    int serve() {
        for (int i = 0; i < 3; ++i) storage_->put("request" + std::to_string(i), "ok");
        logger_->info("served 3 requests");
        return 0;
    }

private:
    void init() override {
        logger_ = &getComponent<Logger>();
        storage_ = &getComponent<Storage>();  // the enabled alternative
        port_ = getComponent<Config>().settings().port;
    }
    void start() override { logger_->info("listening on port " + std::to_string(port_)); }
    void stop() noexcept override { logger_->info("stopped accepting"); }

    const Logger* logger_ = nullptr;
    Storage* storage_ = nullptr;
    int port_ = 0;
};
```

## The composition root

The components in any order, grouped with a bundle. The guard checks a variant this
program doesn't run (one without `Metrics`), so it can't rot. See
[Composing an application](composition.md).

```cpp
using Core = keel::Bundle<Config, Logger>;
using Service = keel::App<HttpServer, Core, Metrics, Database, Migrations, MemoryStorage, DatabaseStorage>;

static_assert(keel::requireValid<HttpServer, Core, Database, Migrations, MemoryStorage, DatabaseStorage>);
static_assert(Service::initOrder()[0] == "Config");

int main(int argc, char** argv) try {
    Service app(argc, argv);
    const int exitCode = app.run([](Service& running) {
        const int code = running.getComponent<HttpServer>().serve();
        for (const keel::Timing& t : running.timings()) {
            std::printf("  %-6s %-16.*s %8.3f ms\n", keel::toString(t.phase).data(),
                        static_cast<int>(t.component.size()), t.component.data(),
                        std::chrono::duration<double, std::milli>(t.duration).count());
        }
        return code;
    });
    return exitCode;
} catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
}
```

Things to try:

- Run it with `db` and with `--no-metrics`, and compare the output and the timings.
- Remove `MemoryStorage` from `Service` and run it without arguments: `init()` fails with
  `keel: HttpServer uses Storage: none of its alternatives (DatabaseStorage) is enabled`,
  and everything already initialized is rolled back.
- Make `Database` depend on `HttpServer`: the build fails with a `DependencyCycle` naming
  the components on the cycle.
- Print `app.toDot()` and render it with Graphviz
  ([Inspecting and testing](inspection-and-testing.md#the-dependency-graph)).
