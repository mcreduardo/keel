// keel/impl/lifecycle.hpp: the out-of-line definitions of ComponentBase and Registry:
// lookups, and the init/start/stop/deinit phases with the async scheduler.
//
// Part of keel.hpp: tools/amalgamate.py joins the parts into the single header.
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include "markers.hpp"
#include "type_name.hpp"
#include "component_base.hpp"
#include "registry.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace keel {

// ===========================================================================
// Implementation

namespace detail {

// The caller's own lifecycle is checked first, so a misplaced lookup fails whether or not
// an optional dependency happens to be composed.
inline void ComponentBase::checkCanUse(std::string_view accessor, std::string_view dependency) const {
    if (registry_ == nullptr) {
        throw Error(concat({"keel: ", accessor, "<", dependency,
                            ">() called before the component was part of an application (from its constructor?). "
                            "Dependencies are available from init() on."}));
    }
    if (!registry_->initCalled()) throw Error(concat({"keel: ", name_, " used ", dependency, " before init()"}));
    if (state_ == State::deinitialized) {
        throw Error(concat({"keel: ", name_, " used ", dependency, " after its deinit()"}));
    }
    if (state_ == State::disabled) {
        throw Error(concat({"keel: ", name_, " used ", dependency, ", but ", name_,
                            " is disabled (its enabled() returned false)"}));
    }
}

inline void* ComponentBase::lookup(TypeId id, std::string_view dependency, bool required) const {
    checkCanUse(required ? "getComponent" : "findComponent", dependency);
    const Binding* b = registry_->resolve(id, dependency, name_, required);
    return b == nullptr ? nullptr : use(*b, dependency);
}

inline const std::vector<Binding>& ComponentBase::lookupAll(TypeId id, std::string_view dependency) const {
    checkCanUse("getAll", dependency);
    return registry_->findAll(id);
}

inline void* ComponentBase::useIfEnabled(const Binding& b, std::string_view dependency) const {
    if (b.owner == this || b.owner->state_ == State::disabled) return nullptr;
    return use(b, dependency);
}

// Runs before init(): the dependencies come earlier in the init order, so each is
// initialized or disabled by now (with async components too: see Registry::Schedule).
// The compile-time checks leave one way to fail: keel::Alternative<>s of which none (for a
// required dependency) or several are enabled.
inline void ComponentBase::checkDependencies() const {
    for (std::size_t i = 0; i < needCount_; ++i) {
        const Need& need = needs_[i];
        lookup(need.id, need.name(), need.use == Use::required);
    }
}

// resolve() never returns a disabled binding.
inline void* ComponentBase::use(const Binding& b, std::string_view dependency) const {
    const State state = b.owner->state_;
    if (state == State::initialized || state == State::started) return b.object;
    if (state == State::created) {
        throw Error(concat({"keel: ", name_, " used ", dependency, " before it was initialized"}));
    }
    throw Error(concat({"keel: ", name_, " used ", dependency, " after it was deinitialized"}));
}

inline const Binding* Registry::resolve(std::span<const Binding> bindings, std::string_view dependency,
                                        std::string_view user, bool required) const {
    bool alternatives = !bindings.empty();  // every binding is an alternative
    bool undecided = false;                 // a binding whose enabled() wasn't asked yet
    const Binding* enabled = nullptr;       // the last one
    std::size_t enabledCount = 0;
    for (const Binding& b : bindings) {
        const ComponentBase::State state = b.owner->state_;
        alternatives = alternatives && b.alternative;
        undecided = undecided || state == ComponentBase::State::created;
        if (state == ComponentBase::State::disabled) continue;
        enabled = &b;
        ++enabledCount;
    }
    const bool chosen = !(alternatives && (state_ == State::building || undecided));
    if (chosen && enabledCount <= 1 && (enabled != nullptr || !required)) return enabled;
    resolveFailed(bindings, dependency, user, alternatives, undecided, enabledCount);
}

// The error of a failed resolve(), in the order resolve() checks. Builds the names only
// here, so that a lookup that succeeds doesn't allocate.
inline void Registry::resolveFailed(std::span<const Binding> bindings, std::string_view dependency,
                                    std::string_view user, bool alternatives, bool undecided,
                                    std::size_t enabledCount) const {
    const auto names = [&](bool enabledOnly) {
        std::string out;
        for (const Binding& b : bindings) {
            if (enabledOnly && b.owner->state_ == ComponentBase::State::disabled) continue;
            out += concat({out.empty() ? "" : ", ", b.owner->name_});
        }
        return out;
    };
    const std::string who = user.empty() ? concat({"keel: getComponent<", dependency, ">()"})
                                         : concat({"keel: ", user, " uses ", dependency});
    if (alternatives && state_ == State::building) {
        throw Error(concat({who, ": init() chooses among its alternatives (", names(false), "); call it after init()"}));
    }
    if (alternatives && undecided) {
        throw Error(concat({who, ": init() failed before choosing among its alternatives (", names(false), ")"}));
    }
    if (enabledCount > 1) {
        throw Error(concat({who, ": several of its alternatives are enabled (only one may be): ", names(true)}));
    }
    if (bindings.empty()) throw Error(concat({who, ", which is not part of the application"}));
    if (alternatives) throw Error(concat({who, ": none of its alternatives (", names(false), ") is enabled"}));
    throw Error(disabledProvider(who, bindings.front().owner->name_, dependency));
}

inline void Registry::checkEnabled(const ComponentBase& provider, std::string_view dependency) const {
    if (provider.state_ != ComponentBase::State::disabled) return;
    throw Error(disabledProvider(concat({"keel: getComponent<", dependency, ">()"}), provider.name_, dependency));
}

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
