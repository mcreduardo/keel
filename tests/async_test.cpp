// keel::AsyncInit and keel::AsyncStart.
//
// The tests don't depend on timing. Where a test claims that two things run at the same
// time, they wait for each other (a Gate or a Meeting): run one after the other, they would
// wait forever. Every wait gives up after `patience` and makes the test fail instead of
// hanging (ctest has a timeout too). Each test runs several rounds to shake out races.
#include <keel.hpp>

#include "check.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace async_test {

constexpr auto patience = std::chrono::seconds(5);
constexpr int rounds = 20;

struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Opened once; wait() returns false if it isn't opened in time.
class Gate {
public:
    void open() {
        {
            const std::lock_guard lock(mutex_);
            open_ = true;
        }
        changed_.notify_all();
    }
    bool wait() {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, patience, [&] { return open_; });
    }
    void reset() { open_ = false; }  // between rounds, when no thread uses it

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool open_ = false;
};

// Like std::latch(parties).arrive_and_wait(), but gives up after `patience`.
class Meeting {
public:
    explicit Meeting(int parties) : parties_(parties) {}
    bool arriveAndWait() {
        std::unique_lock lock(mutex_);
        ++arrived_;
        changed_.notify_all();
        return changed_.wait_for(lock, patience, [&] { return arrived_ >= parties_; });
    }
    void reset() { arrived_ = 0; }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    int parties_;
    int arrived_ = 0;
};

// What ran, and on which thread, from every thread.
class Journal {
public:
    struct Event {
        std::string text;
        std::thread::id thread;
    };
    void add(std::string text) {
        const std::lock_guard lock(mutex_);
        events_.push_back(Event{std::move(text), std::this_thread::get_id()});
    }
    void clear() {
        const std::lock_guard lock(mutex_);
        events_.clear();
    }
    std::vector<std::string> texts() {
        const std::lock_guard lock(mutex_);
        std::vector<std::string> out;
        for (const Event& e : events_) out.push_back(e.text);
        return out;
    }
    bool has(std::string_view text) { return position(text) != npos; }
    // Where text was logged, or npos.
    std::size_t position(std::string_view text) {
        const std::lock_guard lock(mutex_);
        for (std::size_t i = 0; i < events_.size(); ++i) {
            if (events_[i].text == text) return i;
        }
        return npos;
    }
    std::thread::id thread(std::string_view text) {
        const std::lock_guard lock(mutex_);
        for (const Event& e : events_) {
            if (e.text == text) return e.thread;
        }
        return {};
    }
    bool before(std::string_view first, std::string_view second) {
        const std::size_t a = position(first);
        const std::size_t b = position(second);
        return a != npos && b != npos && a < b;
    }
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

private:
    std::mutex mutex_;
    std::vector<Event> events_;
};

Journal journal;

bool onMainThread(std::string_view text) {
    static const std::thread::id mainThread = std::this_thread::get_id();  // tests run on the main thread
    return journal.thread(text) == mainThread;
}

// Logs every hook as "<hook> <class name without namespaces>".
template <class... Entries>
class Logged : public keel::Component<Entries...> {
protected:
    void log(std::string_view hook) const {
        std::string_view name = this->componentName();
        const std::size_t colons = name.rfind("::");
        if (colons != std::string_view::npos) name.remove_prefix(colons + 2);
        journal.add(std::string(hook) + " " + std::string(name));
    }
    void init() override { log("init"); }
    void start() override { log("start"); }
    void stop() noexcept override { log("stop"); }
    void deinit() noexcept override { log("deinit"); }
};

template <class Body>
void inRounds(Body body) {
    for (int round = 0; round < rounds; ++round) {
        journal.clear();
        body();
    }
}

// ---------------------------------------------------------------------------
// Concurrency and order

namespace meet {
Meeting meeting(2);
class Left : public Logged<>, public keel::AsyncInit {
    void init() override {
        log("init");
        if (!meeting.arriveAndWait()) throw Failure("Left waited alone");
    }
};
class Right : public Logged<>, public keel::AsyncInit {
    void init() override {
        log("init");
        if (!meeting.arriveAndWait()) throw Failure("Right waited alone");
    }
};
}  // namespace meet

namespace visible {
class Config : public Logged<> {
public:
    int port = 0;  // not atomic: the scheduler orders the write before the reads

private:
    void init() override {
        log("init");
        port = 8080;
    }
};
class Database : public Logged<Config>, public keel::AsyncInit {
public:
    int port = 0;

private:
    void init() override {
        log("init");
        port = getComponent<Config>().port;
    }
};
class Server : public Logged<Database> {
public:
    int port = 0;

private:
    void init() override {
        log("init");
        port = getComponent<Database>().port;
    }
};
}  // namespace visible

namespace overtake {
Gate otherDone;
class Slow : public Logged<>, public keel::AsyncInit {
    void init() override {
        log("init");
        if (!otherDone.wait()) throw Failure("the calling thread waited for Slow instead of running Other");
    }
};
class AfterSlow : public Logged<Slow> {};
class Other : public Logged<> {
    void init() override {
        log("init");
        otherDone.open();
    }
};
}  // namespace overtake

namespace feeders {
Gate asyncDone;
class Unrelated : public Logged<> {
    void init() override {
        log("init");
        if (!asyncDone.wait()) throw Failure("Unrelated ran before Feeder");
    }
};
class Feeder : public Logged<> {};
class Async : public Logged<Feeder>, public keel::AsyncInit {
    void init() override {
        log("init");
        asyncDone.open();
    }
};
}  // namespace feeders

namespace chain {
Gate secondDone;
class First : public Logged<>, public keel::AsyncInit {};
class Second : public Logged<First>, public keel::AsyncInit {
    void init() override {
        log("init");
        secondDone.open();
    }
};
class Waiter : public Logged<> {
    void init() override {
        log("init");
        if (!secondDone.wait()) throw Failure("the async chain waited for the calling thread");
    }
};
}  // namespace chain

}  // namespace async_test

using namespace async_test;

static_assert(keel::isValid<meet::Left, meet::Right>);
static_assert(keel::App<visible::Server, visible::Database, visible::Config>::initOrder()[1] ==
              keel::typeName<visible::Database>());

TEST(async_inits_run_at_the_same_time) {
    inRounds([] {
        meet::meeting.reset();
        auto app = keel::compose<meet::Left, meet::Right>();
        app.init();  // each init() waits for the other: one after the other, they would fail
        CHECK(!onMainThread("init Left"));
        CHECK(!onMainThread("init Right"));
        CHECK(journal.thread("init Left") != journal.thread("init Right"));
        app.deinit();
        CHECK(onMainThread("deinit Left"));
        CHECK(onMainThread("deinit Right"));
    });
}

TEST(an_async_init_runs_after_its_dependencies_and_sees_their_work) {
    inRounds([] {
        auto app = keel::compose<visible::Server, visible::Database, visible::Config>();
        app.init();
        CHECK((journal.texts() == std::vector<std::string>{"init Config", "init Database", "init Server"}));
        CHECK(onMainThread("init Config"));
        CHECK(!onMainThread("init Database"));
        CHECK(onMainThread("init Server"));
        CHECK(app.getComponent<visible::Database>().port == 8080);
        CHECK(app.getComponent<visible::Server>().port == 8080);
    });
}

TEST(the_calling_thread_runs_other_components_while_an_async_init_runs) {
    inRounds([] {
        overtake::otherDone.reset();
        // Init order: Slow, AfterSlow, Other. AfterSlow waits for Slow, which waits for Other.
        auto app = keel::compose<overtake::Slow, overtake::AfterSlow, overtake::Other>();
        app.init();
        CHECK(journal.before("init Other", "init AfterSlow"));
        CHECK(onMainThread("init AfterSlow"));
    });
}

TEST(the_calling_thread_first_runs_what_an_async_init_waits_for) {
    inRounds([] {
        feeders::asyncDone.reset();
        // Init order: Unrelated, Feeder, Async. Unrelated waits for Async, which needs Feeder.
        auto app = keel::compose<feeders::Unrelated, feeders::Feeder, feeders::Async>();
        app.init();
        CHECK(journal.before("init Feeder", "init Unrelated"));  // Unrelated then waited for Async
    });
}

TEST(a_chain_of_async_inits_does_not_wait_for_the_calling_thread) {
    inRounds([] {
        chain::secondDone.reset();
        auto app = keel::compose<chain::First, chain::Second, chain::Waiter>();
        app.init();  // the calling thread is busy in Waiter until Second is done
        CHECK(journal.before("init First", "init Second"));
        CHECK(!onMainThread("init First"));
        CHECK(!onMainThread("init Second"));
    });
}

// ---------------------------------------------------------------------------
// Ordering edges other than required dependencies

namespace edges {
struct Plugin {
    virtual ~Plugin() = default;
};
struct Cache {
    virtual ~Cache() = default;
    virtual std::string_view kind() const = 0;
};
bool redisWanted = false;

class Redis : public Logged<>, public keel::AsyncInit, public keel::Alternative<Cache>, public Cache,
              public keel::Conditional {
    bool enabled() const override { return redisWanted; }
    std::string_view kind() const override { return "redis"; }
};
class Memory : public Logged<>, public keel::AsyncInit, public keel::Alternative<Cache>, public Cache,
               public keel::Conditional {
    bool enabled() const override { return !redisWanted; }
    std::string_view kind() const override { return "memory"; }
};
class Repository : public Logged<Cache>, public keel::AsyncInit {
public:
    std::string_view cache;

private:
    void init() override {
        log("init");
        cache = getComponent<Cache>().kind();
    }
};

class Spell : public Logged<>, public keel::AsyncInit, public keel::Provides<Plugin>, public Plugin {};
class Grammar : public Logged<>, public keel::Provides<Plugin>, public Plugin {};
class Menu : public Logged<keel::All<Plugin>>, public keel::AsyncInit {
public:
    std::size_t plugins = 0;

private:
    void init() override {
        log("init");
        plugins = getAll<Plugin>().size();
    }
};
class Late : public Logged<keel::After<Menu>>, public keel::AsyncInit {};
class Draft : public Logged<>, public keel::AsyncInit, public keel::Provides<Plugin>, public Plugin,
              public keel::Conditional {
    bool enabled() const override { return false; }
};

class Thrower : public Logged<>, public keel::AsyncInit, public keel::Conditional {
    bool enabled() const override { throw Failure("enabled"); }
};
class Fine : public Logged<> {};

class Probe : public Logged<>, public keel::AsyncInit, public keel::Conditional {
public:
    bool on = true;

private:
    bool enabled() const override {
        journal.add("enabled Probe");
        return on;
    }
};
class Watcher : public Logged<keel::Optional<Probe>> {
public:
    Probe* probe = nullptr;

private:
    void init() override {
        log("init");
        probe = findComponent<Probe>();
    }
};
}  // namespace edges

TEST(async_components_choose_among_alternatives) {
    inRounds([] {
        for (bool redis : {false, true}) {
            journal.clear();
            edges::redisWanted = redis;
            auto app = keel::compose<edges::Repository, edges::Redis, edges::Memory>();
            app.init();
            CHECK(app.getComponent<edges::Repository>().cache == (redis ? "redis" : "memory"));
            CHECK(app.getComponent<edges::Cache>().kind() == (redis ? "redis" : "memory"));
            CHECK(!onMainThread("init Repository"));
        }
    });
}

TEST(all_and_after_order_async_components) {
    inRounds([] {
        auto app = keel::compose<edges::Late, edges::Menu, edges::Spell, edges::Grammar>();
        app.init();
        CHECK(app.getComponent<edges::Menu>().plugins == 2);
        CHECK(journal.before("init Spell", "init Menu"));
        CHECK(journal.before("init Grammar", "init Menu"));
        CHECK(journal.before("init Menu", "init Late"));
        app.deinit();
        CHECK(journal.before("deinit Late", "deinit Menu"));
        CHECK(journal.before("deinit Menu", "deinit Spell"));
        CHECK(journal.before("deinit Menu", "deinit Grammar"));
    });
}

TEST(all_leaves_out_a_disabled_async_provider) {
    inRounds([] {
        auto app = keel::compose<edges::Menu, edges::Spell, edges::Grammar, edges::Draft>();
        app.init();
        CHECK(app.getComponent<edges::Menu>().plugins == 2);
        CHECK(!journal.has("init Draft"));
    });
}

TEST(enabled_may_throw_on_the_async_thread) {
    inRounds([] {
        auto app = keel::compose<edges::Fine, edges::Thrower>();
        std::string error;
        try {
            app.init();
        } catch (const Failure& e) {
            error = e.what();
        }
        CHECK(error == "enabled");
        // Fine has no dependency path to Thrower: whether it ran depends on timing, but if it
        // did, it was rolled back.
        CHECK(journal.has("init Fine") == journal.has("deinit Fine"));
        CHECK(!journal.has("init Thrower"));
    });
}

TEST(enabled_of_an_async_component_is_asked_on_its_thread) {
    inRounds([] {
        for (bool on : {true, false}) {
            journal.clear();
            auto app = keel::compose<edges::Watcher, edges::Probe>();
            app.getComponent<edges::Probe>().on = on;
            app.init();
            CHECK(!onMainThread("enabled Probe"));
            CHECK(journal.has("init Probe") == on);
            if (on) CHECK(journal.thread("enabled Probe") == journal.thread("init Probe"));
            CHECK((app.getComponent<edges::Watcher>().probe != nullptr) == on);
            CHECK(journal.before("enabled Probe", "init Watcher"));
        }
    });
}

// ---------------------------------------------------------------------------
// Failures

namespace failures {
Gate runningStarted;
Gate aboutToThrow;
class Base : public Logged<> {};
class Running : public Logged<Base>, public keel::AsyncInit {
    void init() override {
        log("init");
        runningStarted.open();
        if (!aboutToThrow.wait()) throw Failure("Fails never threw");
        log("init end");
    }
};
class Fails : public Logged<Base>, public keel::AsyncInit {
    void init() override {
        log("init");
        if (!runningStarted.wait()) throw Failure("Running never started");
        aboutToThrow.open();
        throw Failure("Fails");
    }
};
class Dependent : public Logged<Fails> {};
class AsyncDependent : public Logged<Fails>, public keel::AsyncInit {};

Meeting bothRunning(2);
Gate secondThrew;
class FirstToFail : public Logged<>, public keel::AsyncInit {
    void init() override {
        log("init");
        if (!bothRunning.arriveAndWait()) throw Failure("alone");
        // So that SecondToFail most likely fails first in time (it opens the gate just
        // before throwing): a rule "first in time" would then usually, not always, be caught.
        if (!secondThrew.wait()) throw Failure("SecondToFail never threw");
        throw Failure("FirstToFail");
    }
};
class SecondToFail : public Logged<>, public keel::AsyncInit {
    void init() override {
        log("init");
        if (!bothRunning.arriveAndWait()) throw Failure("alone");
        secondThrew.open();
        throw Failure("SecondToFail");
    }
};

Gate asyncStarted;
Gate syncThrowing;
class AsyncWorker : public Logged<>, public keel::AsyncInit {
    void init() override {
        log("init");
        asyncStarted.open();
        if (!syncThrowing.wait()) throw Failure("Bad never threw");
        log("init end");
    }
};
class Bad : public Logged<> {
    void init() override {
        log("init");
        if (!asyncStarted.wait()) throw Failure("AsyncWorker never started");
        syncThrowing.open();
        throw Failure("Bad");
    }
};

void reset() {
    runningStarted.reset();
    aboutToThrow.reset();
    bothRunning.reset();
    secondThrew.reset();
    asyncStarted.reset();
    syncThrowing.reset();
}

std::string initError(auto& app) {
    try {
        app.init();
    } catch (const Failure& e) {
        return e.what();
    }
    return "no exception";
}
}  // namespace failures

TEST(a_failed_async_init_waits_for_the_running_ones_and_rolls_back) {
    inRounds([] {
        failures::reset();
        auto app = keel::compose<failures::Base, failures::Running, failures::Fails, failures::Dependent,
                                 failures::AsyncDependent>();
        CHECK(failures::initError(app) == "Fails");
        // Each call that ran, in the order they finished: Fails and Running in either order.
        std::vector<std::string> calls;
        for (const keel::Timing& t : app.timings()) {
            calls.push_back(std::string(keel::toString(t.phase)) + " " + std::string(t.component) +
                            (t.failed ? " failed" : ""));
        }
        CHECK(calls.size() == 5);
        CHECK(calls[0] == "init " + std::string(keel::typeName<failures::Base>()));
        CHECK(std::count(calls.begin(), calls.end(),
                         "init " + std::string(keel::typeName<failures::Fails>()) + " failed") == 1);
        CHECK(std::count(calls.begin(), calls.end(), "init " + std::string(keel::typeName<failures::Running>())) == 1);
        CHECK(calls[3] == "deinit " + std::string(keel::typeName<failures::Running>()));
        CHECK(calls[4] == "deinit " + std::string(keel::typeName<failures::Base>()));
        // Running was running when Fails threw: init() waited for it, then deinitialized it.
        CHECK(journal.before("init end Running", "deinit Running"));
        CHECK(journal.before("deinit Running", "deinit Base"));
        CHECK(!journal.has("deinit Fails"));
        CHECK(!journal.has("init Dependent"));
        CHECK(!journal.has("init AsyncDependent"));
        CHECK(onMainThread("deinit Running"));
        CHECK_THROWS(app.start());
        app.deinit();  // nothing left to do
        CHECK(journal.texts().back() == "deinit Base");
    });
}

TEST(of_several_failed_inits_the_earliest_in_init_order_is_rethrown) {
    inRounds([] {
        failures::reset();
        auto app = keel::compose<failures::FirstToFail, failures::SecondToFail>();
        CHECK(failures::initError(app) == "FirstToFail");
        int failed = 0;
        for (const keel::Timing& t : app.timings()) failed += t.failed ? 1 : 0;
        CHECK(failed == 2);
    });
}

TEST(a_failed_sync_init_waits_for_the_running_async_ones) {
    inRounds([] {
        failures::reset();
        auto app = keel::compose<failures::AsyncWorker, failures::Bad>();
        CHECK(failures::initError(app) == "Bad");
        CHECK(journal.before("init end AsyncWorker", "deinit AsyncWorker"));
        CHECK(onMainThread("deinit AsyncWorker"));
    });
}

// ---------------------------------------------------------------------------
// AsyncStart

namespace starts {
Meeting bothStarting(2);
Gate feedStarted;
class Config : public Logged<> {};
class Slow : public Logged<Config>, public keel::AsyncInit {};
class Feed : public Logged<Config>, public keel::AsyncStart {
    void start() override {
        log("start");
        if (!bothStarting.arriveAndWait()) throw Failure("Feed started alone");
    }
};
class Ticker : public Logged<>, public keel::AsyncStart {
    void start() override {
        log("start");
        if (!bothStarting.arriveAndWait()) throw Failure("Ticker started alone");
    }
};
class Gateway : public Logged<Feed> {};

class Broken : public Logged<Config>, public keel::AsyncStart {
    void start() override {
        log("start");
        if (!feedStarted.wait()) throw Failure("Starter never started");
        throw Failure("Broken");
    }
};
class Starter : public Logged<Config>, public keel::AsyncStart {
    void start() override {
        log("start");
        feedStarted.open();
    }
};

class Child : public Logged<Broken>, public keel::AsyncStart {};

class Off : public Logged<>, public keel::AsyncStart, public keel::Conditional {
    bool enabled() const override { return false; }
};
class Spinner : public Logged<>, public keel::AsyncStart {};
}  // namespace starts

TEST(async_starts_run_at_the_same_time_after_every_init) {
    inRounds([] {
        starts::bothStarting.reset();
        auto app = keel::compose<starts::Gateway, starts::Feed, starts::Ticker, starts::Slow, starts::Config>();
        app.init();
        CHECK(onMainThread("init Feed"));  // AsyncStart only: init() on the calling thread
        CHECK(!onMainThread("init Slow"));
        app.start();
        CHECK(!onMainThread("start Feed"));
        CHECK(!onMainThread("start Ticker"));
        CHECK(onMainThread("start Gateway"));
        CHECK(journal.before("start Feed", "start Gateway"));
        CHECK(journal.before("init Slow", "start Ticker"));
        app.deinit();
        CHECK(journal.before("stop Gateway", "stop Feed"));
        CHECK(journal.before("stop Feed", "stop Config"));
        CHECK(onMainThread("stop Feed"));
        CHECK(onMainThread("deinit Feed"));
    });
}

TEST(a_failed_async_start_stops_and_deinitializes_everything) {
    inRounds([] {
        starts::feedStarted.reset();
        auto app = keel::compose<starts::Config, starts::Broken, starts::Starter, starts::Child>();
        app.init();
        std::string error;
        try {
            app.start();
        } catch (const Failure& e) {
            error = e.what();
        }
        CHECK(error == "Broken");
        CHECK(journal.has("stop Starter"));  // it had started
        CHECK(!journal.has("stop Broken"));
        CHECK(journal.has("stop Config"));
        CHECK(journal.before("deinit Broken", "deinit Config"));
        CHECK(journal.before("deinit Starter", "deinit Config"));
        CHECK(onMainThread("stop Starter"));
        CHECK(!journal.has("start Child"));  // its thread was created, and skipped
        CHECK(!journal.has("stop Child"));
        CHECK(journal.before("deinit Child", "deinit Broken"));
    });
}

TEST(a_disabled_async_start_component_does_not_start) {
    inRounds([] {
        auto app = keel::compose<starts::Off, starts::Spinner, starts::Config>();
        app.init();
        app.start();
        CHECK(!journal.has("start Off"));
        CHECK(!onMainThread("start Spinner"));  // the scheduler ran this phase
        CHECK(journal.has("start Config"));
        app.deinit();
        CHECK(!journal.has("stop Off"));
    });
}

// ---------------------------------------------------------------------------
// Timings and run

namespace running {
class Config : public Logged<> {};
class Database : public Logged<Config>, public keel::AsyncInit, public keel::AsyncStart {
public:
    int answer() const { return 42; }
};
class Server : public Logged<Database>, public keel::AsyncStart {
public:
    int serve() { return getComponent<Database>().answer(); }
};
using ServerApp = keel::App<Server, Database, Config>;
}  // namespace running

TEST(timings_record_every_call_of_every_thread) {
    inRounds([] {
        running::ServerApp app;
        app.init();
        app.start();
        app.deinit();
        const auto timings = app.timings();
        CHECK(timings.size() == 12);  // 3 components, 4 calls each
        for (const keel::Timing& t : timings) CHECK(!t.failed);
    });
}

TEST(run_works_with_async_components) {
    inRounds([] {
        CHECK(keel::run<running::ServerApp>(&running::Server::serve) == 42);
        CHECK(keel::run<running::ServerApp>([](running::ServerApp& app) {
                  return app.getComponent<running::Database>().answer();
              }) == 42);
        CHECK(!onMainThread("init Database"));
        CHECK(!onMainThread("start Server"));
        CHECK(onMainThread("stop Server"));
    });
}

TEST(to_dot_marks_async_components) {
    running::ServerApp app;
    const std::string dot = app.toDot();
    CHECK(dot.find("Database\\nasync init\\nasync start\"") != std::string::npos);
    CHECK(dot.find("Server\\nasync start\"") != std::string::npos);
    CHECK(dot.find("Config\"") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Inherited async base classes

namespace inherited {
class Config : public Logged<> {};
class Database : public Logged<Config>, public keel::AsyncInit {};
class FastDatabase : public Database {};  // async through its component base class
template <class Base>
class Started : public Base, public keel::AsyncStart {};
class Feed : public Started<Logged<Config>> {};  // keel::AsyncStart through a template base
}  // namespace inherited

TEST(async_base_classes_are_inherited) {
    inRounds([] {
        keel::App<inherited::Config, inherited::FastDatabase, inherited::Feed> app;
        app.init();
        app.start();
        CHECK(onMainThread("init Config"));
        CHECK(!onMainThread("init FastDatabase"));
        CHECK(onMainThread("init Feed"));
        CHECK(!onMainThread("start Feed"));
        app.deinit();
    });
    keel::App<inherited::Config, inherited::FastDatabase, inherited::Feed> app;
    const std::string dot = app.toDot();
    CHECK(dot.find("FastDatabase\\nasync init\"") != std::string::npos);
    CHECK(dot.find("Feed\\nasync start\"") != std::string::npos);
}

// ---------------------------------------------------------------------------
// setAsync(false)

TEST(set_async_false_runs_everything_on_the_calling_thread) {
    inRounds([] {
        running::ServerApp app;
        app.setAsync(false);
        app.init();
        app.start();
        CHECK(onMainThread("init Database"));
        CHECK(onMainThread("start Database"));
        CHECK(onMainThread("start Server"));
        CHECK((journal.texts() == std::vector<std::string>{"init Config", "init Database", "init Server",
                                                           "start Config", "start Database", "start Server"}));
        std::string error;
        try {
            app.setAsync(true);
        } catch (const keel::Error& e) {
            error = e.what();
        }
        CHECK(error == "keel: setAsync() must be called before init()");
    });
}

// ---------------------------------------------------------------------------
// A thread that can't be created (simulated through keel's test hook)

namespace creation {
Gate firstRan;
int created = 0;
int failAt = 0;  // the creation that fails, from 1
void failNth() {
    if (++created != failAt) return;
    // First's hook has run (or is running), so the rollback has something to undo.
    if (!firstRan.wait()) throw Failure("First never ran");
    throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again), "no thread");
}
struct Hook {  // installs the hook for one test, also if it fails
    explicit Hook(int nth) {
        created = 0;
        failAt = nth;
        keel::detail::testHooks.beforeThread = &failNth;
    }
    Hook(const Hook&) = delete;
    Hook& operator=(const Hook&) = delete;
    ~Hook() { keel::detail::testHooks.beforeThread = nullptr; }
};

class First : public Logged<>, public keel::AsyncInit, public keel::AsyncStart {
    void init() override {
        log("init");
        firstRan.open();
    }
    void start() override {
        log("start");
        firstRan.open();
    }
};
class Second : public Logged<>, public keel::AsyncInit, public keel::AsyncStart {};
class Later : public Logged<Second> {};

bool failedEntry(std::span<const keel::Timing> timings, keel::Phase phase) {
    for (const keel::Timing& t : timings) {
        if (t.failed) return t.phase == phase && t.component == keel::typeName<Second>();
    }
    return false;
}
}  // namespace creation

TEST(a_thread_that_cannot_be_created_fails_init_and_rolls_back) {
    inRounds([] {
        creation::firstRan.reset();
        const creation::Hook hook(2);  // Second's thread
        auto app = keel::compose<creation::First, creation::Second, creation::Later>();
        std::string error;
        try {
            app.init();
        } catch (const std::system_error& e) {
            error = e.what();
        }
        CHECK(error.find("no thread") != std::string::npos);
        CHECK(journal.before("init First", "deinit First"));
        CHECK(onMainThread("deinit First"));
        CHECK(!journal.has("init Second"));
        CHECK(!journal.has("init Later"));
        CHECK(creation::failedEntry(app.timings(), keel::Phase::init));
    });
}

TEST(a_thread_that_cannot_be_created_fails_start_and_rolls_back) {
    inRounds([] {
        auto app = keel::compose<creation::First, creation::Second, creation::Later>();
        app.init();
        creation::firstRan.reset();  // no thread uses it between the phases
        const creation::Hook hook(2);
        std::string error;
        try {
            app.start();
        } catch (const std::system_error& e) {
            error = e.what();
        }
        CHECK(error.find("no thread") != std::string::npos);
        CHECK(journal.before("start First", "stop First"));
        CHECK(!journal.has("start Second"));
        CHECK(!journal.has("start Later"));
        CHECK(!journal.has("stop Second"));
        CHECK(journal.before("deinit Later", "deinit Second"));
        CHECK(journal.has("deinit First"));
        CHECK(creation::failedEntry(app.timings(), keel::Phase::start));
    });
}
