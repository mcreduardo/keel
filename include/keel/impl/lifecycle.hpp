// The lifecycle phases of the Registry, and the scheduler for async components.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include "markers.hpp"
#include "introspection.hpp"
#include "component.hpp"
#include "registry.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace keel {

namespace detail {

// For keel's own tests only: called before each thread keel creates (to simulate a failure).
struct TestHooks {
    void (*beforeThread)() = nullptr;
};
inline TestHooks testHooks;

inline void Registry::stopAll() noexcept {
    for (auto it = components_.rbegin(); it != components_.rend(); ++it) {
        ComponentBase& c = **it;
        if (c.state_ != ComponentBase::State::started) continue;
        timed(Phase::stop, c, &ComponentBase::stop);
        c.state_ = ComponentBase::State::initialized;
    }
}

inline void Registry::deinitAll() noexcept {
    for (auto it = components_.rbegin(); it != components_.rend(); ++it) {
        ComponentBase& c = **it;
        if (c.state_ != ComponentBase::State::initialized) continue;
        timed(Phase::deinit, c, &ComponentBase::deinit);
        c.state_ = ComponentBase::State::deinitialized;
    }
}

// Runs one phase when some component is async (see docs/async.md): one thread per async step,
// the calling thread runs the rest, preferring ready steps an async step waits for.
// status_, error_ and failed_ only under mutex_ (run() reads error_ after joining). A step is
// marked done under mutex_ and starts only after reading its dependencies done under mutex_,
// so a dependency's hook happens-before its dependents'. No deadlock: the graph is acyclic.
class Registry::Schedule {
public:
    Schedule(Registry& registry, Phase phase)
        : registry_(registry),
          phase_(phase),
          size_(registry.components_.size()),
          async_(size_, 0),
          feedsAsync_(size_, 0),
          dependencies_(size_),
          status_(size_, Status::pending) {
        std::size_t asyncCount = 0;
        for (std::size_t i = 0; i < size_; ++i) {
            async_[i] = registry_.runsAsync(phase_, i) ? 1 : 0;
            asyncCount += async_[i];
            for (std::size_t j = 0; j < i; ++j) {
                if (registry_.ordered_(i, j)) dependencies_[i].push_back(j);
            }
        }
        for (std::size_t i = size_; i-- > 0;) {
            if (async_[i] == 0 && feedsAsync_[i] == 0) continue;
            for (std::size_t j : dependencies_[i]) feedsAsync_[j] = 1;
        }
        threads_.reserve(asyncCount);
    }
    Schedule(const Schedule&) = delete;
    Schedule& operator=(const Schedule&) = delete;

    // If run() was left early by its own exception (std::mutex::lock can throw), failed_ tells
    // the waiting threads to give up, then they are joined, so none outlives the phase. After
    // run() returns, every thread has been joined already and setting failed_ changes nothing.
    ~Schedule() {
        {
            const std::lock_guard lock(mutex_);
            failed_ = true;
        }
        changed_.notify_all();
        joinAll();
    }

    void run() {
        for (std::size_t i = 0; i < size_; ++i) {
            if (async_[i] == 0) continue;
            try {
                if (testHooks.beforeThread != nullptr) testHooks.beforeThread();
                threads_.emplace_back([this, i] { runAsync(i); });
            } catch (...) {  // std::system_error: no thread was created
                // Recorded like a call that threw, so that timings() shows the failure.
                registry_.record(phase_, *registry_.components_[i], std::chrono::steady_clock::now(), true);
                finish(i, std::current_exception());
                break;
            }
        }
        runLocal();
        joinAll();
        if (error_) std::rethrow_exception(error_);
    }

private:
    enum class Status : unsigned char { pending, running, done };

    void runAsync(std::size_t i) {
        {
            std::unique_lock lock(mutex_);
            changed_.wait(lock, [&] { return failed_ || ready(i); });
            if (failed_) return;  // skipped: the phase is over
            status_[i] = Status::running;
        }
        execute(i);
    }

    void runLocal() {
        std::size_t remaining = 0;
        for (std::size_t i = 0; i < size_; ++i) remaining += async_[i] == 0 ? 1 : 0;
        while (remaining > 0) {
            std::size_t next = size_;
            {
                std::unique_lock lock(mutex_);
                changed_.wait(lock, [&] {
                    next = nextLocal();
                    return failed_ || next != size_;
                });
                if (failed_) return;
                status_[next] = Status::running;
            }
            --remaining;
            execute(next);
        }
    }

    void execute(std::size_t i) {
        std::exception_ptr error;
        try {
            registry_.runOne(phase_, i);
        } catch (...) {
            error = std::current_exception();
        }
        finish(i, std::move(error));
    }

    void finish(std::size_t i, std::exception_ptr error) {
        {
            const std::lock_guard lock(mutex_);
            if (error) {  // the status stays `running`
                if (i < errorAt_) {  // the first in init order is rethrown
                    error_ = std::move(error);
                    errorAt_ = i;
                }
                failed_ = true;
            } else {
                status_[i] = Status::done;
            }
        }
        changed_.notify_all();
    }

    bool ready(std::size_t i) const {
        for (std::size_t j : dependencies_[i]) {
            if (status_[j] != Status::done) return false;
        }
        return true;
    }
    // With mutex_ held: the first ready step an async step waits for, else the first ready one.
    std::size_t nextLocal() const {
        std::size_t first = size_;
        for (std::size_t i = 0; i < size_; ++i) {
            if (async_[i] != 0 || status_[i] != Status::pending || !ready(i)) continue;
            if (feedsAsync_[i] != 0) return i;
            if (first == size_) first = i;
        }
        return first;
    }

    void joinAll() {
        for (std::thread& t : threads_) {
            if (t.joinable()) t.join();
        }
    }

    Registry& registry_;
    const Phase phase_;
    const std::size_t size_;
    std::vector<unsigned char> async_;       // constant after construction
    std::vector<unsigned char> feedsAsync_;  // constant after construction
    std::vector<std::vector<std::size_t>> dependencies_;  // constant: per position, earlier ones it waits for
    std::vector<std::thread> threads_;       // used by the calling thread only

    std::mutex mutex_;
    std::condition_variable changed_;  // notified after each step finishes
    std::vector<Status> status_;
    std::exception_ptr error_;
    std::size_t errorAt_ = size_;
    bool failed_ = false;
};

inline bool Registry::runsAsync(Phase phase, std::size_t i) const noexcept {
    const ComponentBase& c = *components_[i];
    if (phase == Phase::init) return c.asyncInit_;
    return c.asyncStart_ && c.state_ != ComponentBase::State::disabled;  // a disabled one has nothing to run
}

inline void Registry::runOne(Phase phase, std::size_t i) {
    ComponentBase& c = *components_[i];
    if (phase == Phase::init) {
        // Asked first: a disabled component's dependencies aren't checked.
        if (c.conditional_ != nullptr && !c.conditional_->enabled()) {
            c.state_ = ComponentBase::State::disabled;
            return;
        }
        c.checkDependencies();
        timed(Phase::init, c, &ComponentBase::init);
        c.state_ = ComponentBase::State::initialized;
    } else {
        if (c.state_ == ComponentBase::State::disabled) return;
        timed(Phase::start, c, &ComponentBase::start);
        c.state_ = ComponentBase::State::started;
    }
}

inline void Registry::runPhase(Phase phase) {
    bool anyAsync = false;
    if (ordered_ != nullptr && async_) {
        for (std::size_t i = 0; i < components_.size(); ++i) anyAsync = anyAsync || runsAsync(phase, i);
    }
    if (!anyAsync) {  // everything on the calling thread, in init order
        for (std::size_t i = 0; i < components_.size(); ++i) runOne(phase, i);
        return;
    }
    Schedule schedule(*this, phase);
    schedule.run();
}

inline void Registry::init() {
    if (state_ != State::building) throw Error("keel: init() can be called only once per App");
    timings_.reserve(4 * components_.size());  // see timed()
    state_ = State::initializing;
    try {
        runPhase(Phase::init);
    } catch (...) {
        state_ = State::failed;
        deinitAll();  // roll back what was initialized, then report the init() failure
        throw;
    }
    state_ = State::initialized;
}

inline void Registry::start() {
    switch (state_.load()) {
        case State::initialized:
            break;
        case State::building:
            throw Error("keel: start() called before init()");
        case State::initializing:
            throw Error("keel: start() called before init() returned");
        case State::starting:
            throw Error("keel: start() called while start() runs");
        case State::started:
        case State::stopped:
            throw Error("keel: start() can be called only once per App (no restart after stop())");
        case State::failed:
            throw Error("keel: start() after a failed init() or start()");
        case State::deinitialized:
            throw Error("keel: start() called after deinit()");
    }
    state_ = State::starting;
    try {
        runPhase(Phase::start);
    } catch (...) {
        state_ = State::failed;
        stopAll();  // roll back what was started, then everything initialized
        deinitAll();
        throw;
    }
    state_ = State::started;
}

inline void Registry::stop() noexcept {
    if (state_ != State::started) return;
    state_ = State::stopped;
    stopAll();
}

inline void Registry::deinit() noexcept {
    stop();
    if (state_ != State::initialized && state_ != State::stopped) return;
    state_ = State::deinitialized;
    deinitAll();
}

}  // namespace detail

}  // namespace keel
