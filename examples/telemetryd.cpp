// telemetryd: a small telemetry agent. It reads samples from a few (fake) devices,
// aggregates them on a worker pool, and exports a summary. Seven components, each
// declaring what it uses; main() only lists them.
//
// Build: c++ -std=c++20 -I.. telemetryd.cpp -o telemetryd     (Linux: add -pthread)
// Run:   ./telemetryd --rounds 3
//
// On Compiler Explorer (godbolt.org), include the header by URL instead:
//   #include <https://raw.githubusercontent.com/mcreduardo/keel/main/keel.hpp>
// and add "--rounds 3" as execution arguments.
//
// Try it:
//   - remove WorkerPool from the composition at the bottom: compile error,
//     MissingDependency<Aggregator, WorkerPool>;
//   - remove any entry from a component's keel::Component<...> list: compile error at the
//     getComponent<T>() call that uses it (every component uses everything it lists);
//   - call getComponent<DeviceManager>() inside Exporter: compile error, Exporter
//     doesn't list DeviceManager.
#include "keel.hpp"

#include <atomic>
#include <charconv>
#include <condition_variable>
#include <cstdio>
#include <exception>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

// --- CliArgs: the parsed command line -----------------------------------------
// A bad command line throws UsageError; main() reports it and exits with 2.
struct UsageError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class CliArgs : public keel::Component<> {
public:
    CliArgs(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            const std::string flag = argv[i];
            if (flag != "--config" && flag != "--rounds") throw UsageError("unknown argument: " + flag);
            if (i + 1 == argc) throw UsageError(flag + " needs a value");
            const std::string value = argv[++i];
            if (flag == "--config") configPath_ = value;
            if (flag == "--rounds") rounds_ = positive(flag, value);
        }
    }
    const std::string& configPath() const { return configPath_; }
    int rounds() const { return rounds_; }

private:
    static int positive(const std::string& flag, const std::string& value) {
        int n = 0;
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), n);
        if (error != std::errc() || end != value.data() + value.size() || n <= 0) {
            throw UsageError(flag + " needs a positive number, not '" + value + "'");
        }
        return n;
    }

    std::string configPath_ = "telemetryd.conf";
    int rounds_ = 3;
};

// --- Config: settings loaded from the file CliArgs points to ------------------
class Config : public keel::Component<CliArgs> {
public:
    const std::string& path() const { return path_; }
    const std::vector<std::string>& devices() const { return devices_; }
    int workers() const { return workers_; }
    const std::string& endpoint() const { return endpoint_; }

private:
    void init() override {
        path_ = getComponent<CliArgs>().configPath();
        // A real agent parses the file at path_. Fixed values here.
        devices_ = {"imu0", "gps0", "baro0"};
        workers_ = 2;
        endpoint_ = "collector.local:9000";
    }

    std::string path_;
    std::vector<std::string> devices_;
    int workers_ = 1;
    std::string endpoint_;
};

// --- Logger -------------------------------------------------------------------
class Logger : public keel::Component<Config> {
public:
    void info(const std::string& message) const {
        std::lock_guard lock(mutex_);
        std::printf("[info] %s\n", message.c_str());
    }

private:
    void init() override { info("logger up, config " + getComponent<Config>().path()); }
    void deinit() noexcept override { info("logger down"); }

    mutable std::mutex mutex_;
};

// --- DeviceManager: open device handles (fake hardware) -----------------------
class DeviceManager : public keel::Component<Config, Logger> {
public:
    std::size_t count() const { return devices_.size(); }
    double read(std::size_t device) { return 10.0 * static_cast<double>(device + 1) + static_cast<double>(++reads_ % 3); }
    const std::string& name(std::size_t device) const { return devices_[device]; }

private:
    void init() override {
        logger_ = &getComponent<Logger>();
        devices_ = getComponent<Config>().devices();
        for (const auto& d : devices_) logger_->info("opened " + d);
    }
    void deinit() noexcept override {
        for (const auto& d : devices_) logger_->info("closed " + d);  // the Logger is still up
    }

    const Logger* logger_ = nullptr;
    std::vector<std::string> devices_;
    std::atomic<int> reads_{0};
};

// --- WorkerPool: threads ------------------------------------------------------
class WorkerPool : public keel::Component<Config, Logger> {
public:
    std::future<double> submit(std::function<double()> task) {
        std::packaged_task<double()> job(std::move(task));
        auto result = job.get_future();
        {
            std::lock_guard lock(mutex_);
            queue_.push(std::move(job));
        }
        wake_.notify_one();
        return result;
    }

private:
    void init() override {
        logger_ = &getComponent<Logger>();
        const int n = getComponent<Config>().workers();
        for (int i = 0; i < n; ++i) threads_.emplace_back([this] { work(); });
        logger_->info("worker pool up, " + std::to_string(n) + " threads");
    }
    void deinit() noexcept override {
        {
            std::lock_guard lock(mutex_);
            done_ = true;
        }
        wake_.notify_all();
        for (auto& t : threads_) t.join();
        logger_->info("worker pool down");  // safe: Logger outlives us, keel guarantees it
    }
    void work() {
        for (;;) {
            std::packaged_task<double()> job;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [this] { return done_ || !queue_.empty(); });
                if (done_ && queue_.empty()) return;
                job = std::move(queue_.front());
                queue_.pop();
            }
            job();
        }
    }

    const Logger* logger_ = nullptr;
    std::vector<std::thread> threads_;
    std::queue<std::packaged_task<double()>> queue_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool done_ = false;
};

// --- Aggregator: business logic -----------------------------------------------
class Aggregator : public keel::Component<DeviceManager, WorkerPool, Logger> {
public:
    // Reads every device on the worker pool and returns the mean of this round.
    double collect() {
        std::vector<std::future<double>> reads;
        for (std::size_t d = 0; d < devices_->count(); ++d) {
            reads.push_back(pool_->submit([this, d] { return devices_->read(d); }));
        }
        double sum = 0;
        for (auto& r : reads) sum += r.get();
        ++rounds_;
        return sum / static_cast<double>(reads.size());
    }
    int rounds() const { return rounds_; }

private:
    void init() override {
        devices_ = &getComponent<DeviceManager>();
        pool_ = &getComponent<WorkerPool>();
        getComponent<Logger>().info("aggregating " + std::to_string(devices_->count()) + " devices");
    }

    DeviceManager* devices_ = nullptr;
    WorkerPool* pool_ = nullptr;
    int rounds_ = 0;
};

// --- Exporter: the top of the graph -------------------------------------------
class Exporter : public keel::Component<Aggregator, Config, CliArgs, Logger> {
public:
    int run() {
        for (int i = 0; i < rounds_; ++i) {
            const double mean = aggregator_->collect();
            logger_->info("export to " + endpoint_ + ": mean=" + std::to_string(mean));
        }
        return 0;
    }

private:
    void init() override {
        aggregator_ = &getComponent<Aggregator>();
        logger_ = &getComponent<Logger>();
        endpoint_ = getComponent<Config>().endpoint();
        rounds_ = getComponent<CliArgs>().rounds();
    }

    Aggregator* aggregator_ = nullptr;
    const Logger* logger_ = nullptr;
    std::string endpoint_;
    int rounds_ = 0;
};

// --- The composition root -----------------------------------------------------
// Every component of the application, in any order. keel checks the list at compile
// time and derives the init order from the declared dependencies.
using Telemetryd = keel::App<Exporter, Aggregator, WorkerPool, DeviceManager, Logger, Config, CliArgs>;

int main(int argc, char** argv) try {
    return keel::run<Telemetryd>(&Exporter::run, argc, argv);
} catch (const UsageError& e) {
    std::fprintf(stderr, "telemetryd: %s\nusage: telemetryd [--config <file>] [--rounds <n>]\n", e.what());
    return 2;
} catch (const std::exception& e) {
    std::fprintf(stderr, "telemetryd: %s\n", e.what());
    return 1;
}
