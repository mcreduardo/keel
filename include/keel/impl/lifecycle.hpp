// keel/impl/lifecycle.hpp: the init/start/stop/deinit phases of the Registry, and the
// scheduler that runs keel::AsyncInit and keel::AsyncStart components on their own threads.
//
// Part of keel.hpp: tools/amalgamate.py joins the parts into the single header.
//
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

// For keel's own tests only: called on the thread that drives the App before each thread
// keel creates. A test makes it throw std::system_error to simulate a failed creation.
struct TestHooks {
    void (*beforeThread)() = nullptr;
};
inline TestHooks testHooks;

// Stops every started component, in reverse order.
inline void Registry::stopAll() noexcept {
    for (auto it = components_.rbegin(); it != components_.rend(); ++it) {
        ComponentBase& c = **it;
        if (c.state_ != ComponentBase::State::started) continue;
        timed(Phase::stop, c, &ComponentBase::stop);
        c.state_ = ComponentBase::State::initialized;
    }
}

// Deinitializes every initialized component, in reverse order.
inline void Registry::deinitAll() noexcept {
    for (auto it = components_.rbegin(); it != components_.rend(); ++it) {
        ComponentBase& c = **it;
        if (c.state_ != ComponentBase::State::initialized) continue;
        timed(Phase::deinit, c, &ComponentBase::deinit);
        c.state_ = ComponentBase::State::deinitialized;
    }
}

// Runs one phase (init or start) when some component runs it on its own thread.
//
// Every component of the App is a step. A step is ready when every step it is ordered
// after is done. Each async component gets a thread when the phase begins, which waits
// until its step is ready and runs it. The calling thread runs the other steps. When it
// next picks a step, it takes the first ready one in init order that some async component
// waits for (directly or through others), else the first ready one; it waits only when
// none is ready.
//
// Synchronization: status_, errors_ and failed_ are read and written only with mutex_
// held, except that run() reads errors_ after joining every thread. The other fields are
// constant once the threads exist, or used by the calling thread only. A step marks
// itself done under mutex_ after it returns, and a step starts only after reading, under
// mutex_, that its dependencies are done. So everything a dependency's hook did (its
// component's members, its state_) happens before its dependents' hooks run, on whichever
// threads. Steps with no ordering path between them may run concurrently; keel's part of
// a step writes only its own component's state_, and a component's lookups read only the
// state_ of the components it is ordered after. (A hook that calls App::getComponent<T>()
// on the App itself may read any component's state_: atomic, so not a data race, but the
// answer depends on timing.)
//
// Failure: the first failure sets failed_. From then on no step starts; steps already
// running finish. run() joins every thread, then rethrows the exception of the earliest
// failed step in init order. A failed step stays `running`, and a skipped one `pending`:
// only `done` lets anything continue. The destructor joins too, if run() didn't get that
// far.
// No deadlock: the graph is acyclic, so while nothing has failed, some pending step has
// all its dependencies done, and the thread that runs it is woken by the last of them.
class Registry::Schedule {
public:
    Schedule(Registry& registry, Phase phase)
        : registry_(registry),
          phase_(phase),
          size_(registry.components_.size()),
          async_(size_, 0),
          feedsAsync_(size_, 0),
          status_(size_, Status::pending),
          errors_(size_) {
        std::size_t asyncCount = 0;
        for (std::size_t i = 0; i < size_; ++i) {
            async_[i] = registry_.runsAsync(phase_, i) ? 1 : 0;
            asyncCount += async_[i];
        }
        // Reverse init order: each dependent is visited before its dependencies.
        for (std::size_t i = size_; i-- > 0;) {
            if (async_[i] == 0 && feedsAsync_[i] == 0) continue;
            for (std::size_t j : registry_.dependencies_[i]) feedsAsync_[j] = 1;
        }
        threads_.reserve(asyncCount);
    }
    Schedule(const Schedule&) = delete;
    Schedule& operator=(const Schedule&) = delete;

    // Nothing left to do after run() returned or rethrew a step's exception. If run() was
    // left early by an exception of its own (std::mutex::lock can throw std::system_error),
    // tells the waiting threads to give up and joins every thread, so none outlives the
    // phase.
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
        for (const std::exception_ptr& error : errors_) {  // in init order
            if (error) std::rethrow_exception(error);
        }
    }

private:
    enum class Status : unsigned char { pending, running, done };

    // On the thread of async step i.
    void runAsync(std::size_t i) {
        {
            std::unique_lock lock(mutex_);
            changed_.wait(lock, [&] { return failed_ || ready(i); });
            if (failed_) return;  // skipped: the phase is over
            status_[i] = Status::running;
        }
        execute(i);
    }

    // On the calling thread: the steps that aren't async, until they are all done or a
    // step failed.
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

    // Runs step i on the current thread, then records the outcome and wakes the waiters.
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
            if (error) {
                errors_[i] = std::move(error);  // the status stays `running`
                failed_ = true;
            } else {
                status_[i] = Status::done;
            }
        }
        changed_.notify_all();
    }

    // With mutex_ held.
    bool ready(std::size_t i) const {
        for (std::size_t j : registry_.dependencies_[i]) {
            if (status_[j] != Status::done) return false;
        }
        return true;
    }
    // With mutex_ held: the first ready pending step of the calling thread that an async
    // step waits for, else the first ready one, else size_.
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
    std::vector<std::thread> threads_;       // used by the calling thread only

    std::mutex mutex_;
    std::condition_variable changed_;  // notified after each step finishes
    std::vector<Status> status_;
    std::vector<std::exception_ptr> errors_;
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
        // Asked first: a component that is off isn't checked (a disabled alternative's
        // keel::Optional<I> isn't reported if several I are enabled, say).
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
    if (hasAsync_ && async_) {
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
    state_ = State::failed;  // until proven otherwise
    try {
        runPhase(Phase::init);
    } catch (...) {
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
        case State::started:
        case State::stopped:
            throw Error("keel: start() can be called only once per App (no restart after stop())");
        case State::failed:
            throw Error("keel: start() after a failed init() or start()");
        case State::deinitialized:
            throw Error("keel: start() called after deinit()");
    }
    state_ = State::failed;  // until proven otherwise
    try {
        runPhase(Phase::start);
    } catch (...) {
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
