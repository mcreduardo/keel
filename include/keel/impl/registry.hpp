// keel/impl/registry.hpp: the runtime state of an App (Registry), and the lookups: what a
// dependency resolves to, and ComponentBase's lookup functions, defined here because they
// need the Registry.
//
// Part of keel.hpp: tools/amalgamate.py joins the parts into the single header.
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include "markers.hpp"
#include "introspection.hpp"
#include "type_name.hpp"
#include "lists.hpp"
#include "component.hpp"
#include "component_info.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace keel {

namespace detail {

// ===========================================================================
// Runtime state of an App: the components in init order, and lookups by type.

class Registry {
public:
    Registry() = default;
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    template <class C>
    void add(C& component) {
        ComponentBase& base = component;
        base.registry_ = this;
        base.needs_ = Needs<C>::table.data();
        base.needCount_ = Needs<C>::table.size();
        base.name_ = typeName<C>();
        if constexpr (isConditional<C>) base.conditional_ = &component;
        base.asyncInit_ = hasAsyncInit<C>;
        base.asyncStart_ = hasAsyncStart<C>;
        components_.push_back(&base);
        dependencies_.emplace_back();
    }

    // The lookups of one type: the App binds each type an entry of a component's list refers
    // to, once, to its providers in init order. (Types nothing refers to have no bindings:
    // App::getComponent<T>() finds a single provider in its storage.)
    bool beginBindings(TypeId id) { return bindings_.try_emplace(id).second; }  // false if bound already
    void bind(TypeId id, Binding binding) { bindings_[id].push_back(binding); }

    // The component at init position `dependent` is ordered after the one at `dependency`
    // (an earlier position). Only needed, and only added, when some component is async.
    void addDependency(std::size_t dependent, std::size_t dependency) {
        dependencies_[dependent].push_back(dependency);
    }
    // Some component is async: the phases may need the scheduler (else it is never used).
    void setHasAsync() noexcept { hasAsync_ = true; }
    void setAsync(bool async) {
        if (state_ != State::building) throw Error("keel: setAsync() must be called before init()");
        async_ = async;
    }

    void init();
    void start();
    void stop() noexcept;
    void deinit() noexcept;

    /// init() was called: components may look up their dependencies.
    bool initCalled() const noexcept { return state_ != State::building; }

    // What a dependency on one id resolves to: its one bound component that isn't
    // disabled, or nullptr if there is none and !required. Several bindings exist only for
    // keel::Alternative<>s (else the lookup doesn't compile). Throws if there is none and
    // required, if several alternatives are enabled, or if init() hasn't chosen yet. user
    // names the component that asks, for the messages; empty for the composition root.
    // (For a component, a required dependency on a single disabled keel::Conditional
    // doesn't compile; the root's getComponent<T>() can still meet one.)
    const Binding* resolve(TypeId id, std::string_view dependency, std::string_view user, bool required) const {
        return resolve(findAll(id), dependency, user, required);
    }
    // The same among these bindings (the root's lookup of an interface, which it finds itself).
    const Binding* resolve(std::span<const Binding> bindings, std::string_view dependency, std::string_view user,
                           bool required) const;
    // For the root's getComponent<T>() on T's single provider: throws if init() disabled it.
    void checkEnabled(const ComponentBase& provider, std::string_view dependency) const;

    // Every component bound to id, in init order (empty if none).
    const std::vector<Binding>& findAll(TypeId id) const {
        static const std::vector<Binding> none;
        auto it = bindings_.find(id);
        return it == bindings_.end() ? none : it->second;
    }

    std::span<const Timing> timings() const noexcept { return timings_; }

    // The component at this position of the init order is a keel::Conditional whose
    // enabled() returned false.
    bool disabled(std::size_t position) const noexcept {
        return components_[position]->state_ == ComponentBase::State::disabled;
    }

private:
    // stopped vs initialized: the components are in the same state, but a stopped app
    // can't be started again.
    enum class State { building, initialized, started, stopped, failed, deinitialized };

    class Schedule;

    static std::string disabledProvider(std::string_view who, std::string_view provider, std::string_view dependency) {
        return concat({who, ": ", provider == dependency ? "" : "its provider ", provider,
                       " is disabled (its enabled() returned false)"});
    }
    // Throws the error of a resolve() that found no usable binding.
    [[noreturn]] void resolveFailed(std::span<const Binding> bindings, std::string_view dependency,
                                    std::string_view user, bool alternatives, bool undecided,
                                    std::size_t enabledCount) const;

    // Runs init() or start() on every component (Phase::init or Phase::start), each after
    // the ones it is ordered after; the async ones on their own threads (Schedule). Throws
    // after every thread has finished; the caller rolls back.
    void runPhase(Phase phase);
    // The init or start step of the component at init position i, on the current thread.
    void runOne(Phase phase, std::size_t i);
    // The component at init position i runs this phase on its own thread.
    bool runsAsync(Phase phase, std::size_t i) const noexcept;

    bool hasAsync_ = false;  // some component is async (known at compile time)
    bool async_ = true;      // App::setAsync()

    void stopAll() noexcept;
    void deinitAll() noexcept;

    // Calls hook on c and records how long it took, also if it throws. init() reserves
    // room for every call an App can make (at most one per component and phase), so
    // recording never allocates, also in stop() and deinit(), which must not throw.
    template <class Hook>
    void timed(Phase phase, ComponentBase& c, Hook hook) {
        struct Recorder {  // records when the call returns or throws
            Recorder(Registry& r, Phase p, const ComponentBase& c) : registry(r), phase(p), component(c) {}
            Recorder(const Recorder&) = delete;
            Recorder& operator=(const Recorder&) = delete;
            ~Recorder() { registry.record(phase, component, begin, failed); }
            Registry& registry;
            Phase phase;
            const ComponentBase& component;
            std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
            bool failed = true;  // until the call returns
        } recorder(*this, phase, c);
        (c.*hook)();
        recorder.failed = false;
    }
    // May run on several threads at once (async components): the entries are appended
    // under timingsMutex_, in the order the calls finish.
    void record(Phase phase, const ComponentBase& c, std::chrono::steady_clock::time_point begin,
                bool failed) noexcept {
        const auto duration = std::chrono::steady_clock::now() - begin;
        const std::lock_guard lock(timingsMutex_);
        // Can't be full (see timed()); checked anyway, since a push_back that allocated
        // could throw here, in a noexcept function.
        assert(timings_.size() < timings_.capacity());
        if (timings_.size() == timings_.capacity()) return;
        timings_.push_back(
            Timing{c.name_, phase, std::chrono::duration_cast<std::chrono::nanoseconds>(duration), failed});
    }

    // Built by the App's constructor, then only read (also by several threads at once).
    std::vector<ComponentBase*> components_;  // in init order
    std::vector<std::vector<std::size_t>> dependencies_;  // per init position: earlier positions it waits for
    std::unordered_map<TypeId, std::vector<Binding>> bindings_;  // each in init order

    std::mutex timingsMutex_;
    std::vector<Timing> timings_;  // in the order the calls finished

    // Written only by the thread that drives the App (init(), start(), ...), and never while
    // a phase's threads run. Atomic for lookups from threads the components start themselves.
    std::atomic<State> state_ = State::building;
};

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

}  // namespace detail

}  // namespace keel
