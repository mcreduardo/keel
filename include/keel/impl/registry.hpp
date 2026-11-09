// The runtime state of an App (Registry) and the lookups, including ComponentBase's.
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

class Registry {
public:
    Registry() = default;
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    template <class C>
    void add(C& component) {
        ComponentBase& base = component;
        base.registry_ = this;
        base.needs_ = Needs<C>::table;
        base.name_ = typeName<C>();
        if constexpr (isConditional<C>) base.conditional_ = &component;
        base.asyncInit_ = hasAsyncInit<C>;
        base.asyncStart_ = hasAsyncStart<C>;
        components_.push_back(&base);
    }

    // Only types some entry refers to get bindings; App::getComponent<T>() builds its own.
    void bind(TypeId id, Binding binding) { bindings_[id].push_back(binding); }

    // Set only when some component is async. ordered(k, m): the k-th component in init order
    // waits for the m-th (m < k).
    void setOrdering(bool (*ordered)(std::size_t, std::size_t)) noexcept { ordered_ = ordered; }
    void setAsync(bool async) {
        if (state_ != State::building) throw Error("keel: setAsync() must be called before init()");
        async_ = async;
    }

    void init();
    void start();
    void stop() noexcept;
    void deinit() noexcept;

    bool initCalled() const noexcept { return state_ != State::building; }

    // Several bindings exist only for keel::Alternative<>s. user is empty for the composition root,
    // which, unlike a component, can meet a required dependency on a disabled keel::Conditional.
    const Binding* resolve(TypeId id, std::string_view dependency, std::string_view user, bool required) const {
        return resolve(findAll(id), dependency, user, required);
    }
    const Binding* resolve(std::span<const Binding> bindings, std::string_view dependency, std::string_view user,
                           bool required) const;

    const std::vector<Binding>& findAll(TypeId id) const {
        static const std::vector<Binding> none;
        auto it = bindings_.find(id);
        return it == bindings_.end() ? none : it->second;
    }

    std::span<const Timing> timings() const noexcept { return timings_; }

    bool disabled(std::size_t position) const noexcept {
        return components_[position]->state_ == ComponentBase::State::disabled;
    }

private:
    // stopped vs initialized: same component states, but a stopped app can't be started again.
    // initializing and starting: while init() or start() runs (a lookup may come from a hook).
    enum class State { building, initializing, initialized, starting, started, stopped, failed, deinitialized };

    class Schedule;

    [[noreturn]] void resolveFailed(std::span<const Binding> bindings, std::string_view dependency,
                                    std::string_view user, bool alternatives, bool undecided,
                                    std::size_t enabledCount) const;

    // Throws after every thread has finished; the caller rolls back.
    void runPhase(Phase phase);
    void runOne(Phase phase, std::size_t i);
    bool runsAsync(Phase phase, std::size_t i) const noexcept;

    bool (*ordered_)(std::size_t, std::size_t) = nullptr;  // null: no component is async
    bool async_ = true;      // App::setAsync()

    void stopAll() noexcept;
    void deinitAll() noexcept;

    // init() reserves room for every call an App can make, so recording never allocates,
    // also in the noexcept stop() and deinit().
    template <class Hook>
    void timed(Phase phase, ComponentBase& c, Hook hook) {
        const auto begin = std::chrono::steady_clock::now();
        try {
            (c.*hook)();
        } catch (...) {
            record(phase, c, begin, true);
            throw;  // unchanged
        }
        record(phase, c, begin, false);
    }
    // May run on several threads at once; appends under timingsMutex_.
    void record(Phase phase, const ComponentBase& c, std::chrono::steady_clock::time_point begin,
                bool failed) noexcept {
        const auto duration = std::chrono::steady_clock::now() - begin;
        const std::lock_guard lock(timingsMutex_);
        // Can't be full: at most 4 calls per component (init, start, stop, deinit; see timed()).
        // Checked since an allocating push_back could throw in noexcept code.
        assert(timings_.size() < timings_.capacity());
        if (timings_.size() == timings_.capacity()) return;
        timings_.push_back(
            Timing{c.name_, phase, std::chrono::duration_cast<std::chrono::nanoseconds>(duration), failed});
    }

    // Built by the App's constructor, then only read (also by several threads at once).
    std::vector<ComponentBase*> components_;  // in init order
    std::unordered_map<TypeId, std::vector<Binding>> bindings_;  // each in init order

    std::mutex timingsMutex_;
    std::vector<Timing> timings_;  // in the order the calls finished

    // Written only by the driving thread, never while a phase's threads run. Atomic for lookups
    // from threads the components start themselves.
    std::atomic<State> state_ = State::building;
};

// The caller's own lifecycle is checked first, so a misplaced lookup fails whether or not an
// optional dependency happens to be composed.
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

// The dependencies are initialized or disabled by now, async too (see Registry::Schedule).
inline void ComponentBase::checkDependencies() const {
    for (const Need& need : needs_) lookup(need.id, need.name(), need.use == Use::required);
}

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

// Builds the names only here, so that a lookup that succeeds doesn't allocate.
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
    if (alternatives && undecided && state_ == State::initializing) {
        throw Error(concat({who, ": init() hasn't chosen among its alternatives yet (", names(false), ")"}));
    }
    if (alternatives && undecided) {
        throw Error(concat({who, ": init() failed before choosing among its alternatives (", names(false), ")"}));
    }
    if (enabledCount > 1) {
        throw Error(concat({who, ": several of its alternatives are enabled (only one may be): ", names(true)}));
    }
    if (bindings.empty()) throw Error(concat({who, ", which is not part of the application"}));
    if (alternatives) throw Error(concat({who, ": none of its alternatives (", names(false), ") is enabled"}));
    const std::string_view provider = bindings.front().owner->name_;
    throw Error(concat({who, ": ", provider == dependency ? "" : "its provider ", provider,
                        " is disabled (its enabled() returned false)"}));
}

}  // namespace detail

}  // namespace keel
