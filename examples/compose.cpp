// compose: components, declared dependencies, and the composition root.
//
//   Greeter ──> Settings
//      │  └───> Console
//      └ ─ ─ ─> Clock (optional) ──> Console
//
// The compose<...> list can be in any order: keel checks it at compile time and derives
// the init order from the dependencies. The second composition leaves Clock out, which is
// fine because Greeter declares it keel::Optional.
//
// Build: c++ -std=c++20 -I.. compose.cpp -o compose     (Linux: add -pthread)
// Run:   ./compose Ada

#include "keel.hpp"

#include <cstdio>
#include <exception>
#include <string>

// No dependencies. Receives compose()'s arguments in its constructor.
class Settings : public keel::Component<> {
public:
    Settings(int argc, char** argv) : name_(argc > 1 ? argv[1] : "world") {}
    const std::string& name() const { return name_; }

private:
    std::string name_;
};

class Console : public keel::Component<> {
public:
    void print(const std::string& line) const { std::printf("%s\n", line.c_str()); }

private:
    void init() override { print("[console] init"); }
    void deinit() noexcept override { print("[console] deinit"); }
};

// Counts the greetings; uses the Console to report.
class Clock : public keel::Component<Console> {
public:
    int tick() { return ++ticks_; }

private:
    void init() override {
        console_ = &getComponent<Console>();
        console_->print("[clock] init");
    }
    void deinit() noexcept override { console_->print("[clock] deinit"); }  // Console is still up

    const Console* console_ = nullptr;
    int ticks_ = 0;
};

// Uses Settings and Console, and Clock if the application has one.
class Greeter : public keel::Component<Settings, Console, keel::Optional<Clock>> {
public:
    void greet() {
        const std::string tick = clock_ != nullptr ? " (tick " + std::to_string(clock_->tick()) + ")" : "";
        console_->print("hello, " + settings_->name() + tick);
    }

private:
    void init() override {
        // Dependencies are usable from init() on: fetch them once and keep them.
        settings_ = &getComponent<Settings>();
        console_ = &getComponent<Console>();
        clock_ = findComponent<Clock>();  // nullptr if Clock isn't composed
        console_->print("[greeter] init");
    }
    void deinit() noexcept override { console_->print("[greeter] deinit"); }

    const Settings* settings_ = nullptr;
    const Console* console_ = nullptr;
    Clock* clock_ = nullptr;
};

int main(int argc, char** argv) try {
    std::printf("== with a Clock\n");
    {
        // Any order: Console and Settings are initialized first, Greeter last.
        auto app = keel::compose<Greeter, Clock, Console, Settings>(argc, argv);
        app.init();
        app.getComponent<Greeter>().greet();
        app.getComponent<Greeter>().greet();
    }  // the destructor deinitializes, in reverse order

    std::printf("== without a Clock\n");
    {
        auto app = keel::compose<Greeter, Console, Settings>(argc, argv);
        app.init();
        app.getComponent<Greeter>().greet();
    }

    // keel::compose<Greeter, Settings>(argc, argv);  // compile error: MissingDependency<Greeter, Console>
} catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
}
