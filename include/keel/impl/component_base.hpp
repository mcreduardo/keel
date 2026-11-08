// keel/impl/component_base.hpp: what keel reads from a component's keel::Component<...> list, and
// ComponentBase, the type-erased base of every component.
//
// Part of keel.hpp: tools/amalgamate.py joins the parts into the single header.
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include "markers.hpp"
#include "type_list.hpp"
#include "split.hpp"
#include "type_name.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <string_view>
#include <type_traits>
#include <vector>

namespace keel {

namespace detail {

// The keel::Component<...> base of a component C, deduced, so that members of C named like
// Component's own (Entries, ListedAlternatives, ...) don't hide them. Never defined.
template <class... Dependencies>
Component<Dependencies...>* componentBaseOf(Component<Dependencies...>*);
// A class template, so that each C is deduced once (an alias would deduce on every use).
template <class C>
struct ComponentInfo {
    using Base = std::remove_pointer_t<decltype(componentBaseOf(static_cast<C*>(nullptr)))>;
    using Entries = typename Base::Entries;
    using Listed = Concat<typename Base::ListedProvides, typename Base::ListedAlternatives>;
    using Alternatives = typename Base::ListedAlternatives;
};
template <class C>
using ComponentOf = typename ComponentInfo<C>::Base;
template <class C>
using EntriesOf = typename ComponentInfo<C>::Entries;
// The interfaces C names in its list by mistake: keel::Provides<I> and keel::Alternative<I>
// (ProvidesAsDependency, AlternativeAsDependency). They count as provided, so that the
// mistake is the one error.
template <class C>
using ListedOf = typename ComponentInfo<C>::Listed;
template <class C>
using ListedAlternativesOf = typename ComponentInfo<C>::Alternatives;
template <class C>
inline constexpr bool uniqueEntriesOf = ComponentOf<C>::uniqueEntries;

class Registry;
struct Binding;

// An entry of a component's keel::Component<...> list that init() checks (a required
// dependency or Optional<T>, which may resolve to keel::Alternative<>s chosen at runtime):
// the type it names and how it is used.
struct Need {
    TypeId id;
    std::string_view (*name)();
    Use use;
};
template <class Entry>
inline constexpr bool checkedAtInit = Split<Entry>::use == Use::required || Split<Entry>::use == Use::optional;

template <class... Entries>
inline constexpr std::array<Need, sizeof...(Entries)> needsOf{
    {Need{typeId<typename Split<Entries>::target>(), &typeName<typename Split<Entries>::target>,
          Split<Entries>::use}...}};
template <>
inline constexpr std::array<Need, 0> needsOf<>{};

template <class List>
struct NeedsImpl;
template <class... Entries>
struct NeedsImpl<TypeList<Entries...>> {
    using Checked = Concat<std::conditional_t<checkedAtInit<Entries>, TypeList<Entries>, TypeList<>>...>;
};
template <class List>
struct NeedsTable;
template <class... Entries>
struct NeedsTable<TypeList<Entries...>> {
    static constexpr const std::array<Need, sizeof...(Entries)>& table = needsOf<Entries...>;
};
// The checked entries of C's list.
template <class C>
using Needs = NeedsTable<typename NeedsImpl<EntriesOf<C>>::Checked>;

// ===========================================================================
// Type-erased base of every component. Users derive from keel::Component<...>.

class ComponentBase {
public:
    ComponentBase() = default;
    ComponentBase(const ComponentBase&) = delete;
    ComponentBase& operator=(const ComponentBase&) = delete;
    virtual ~ComponentBase() = default;

    /// The component's type name, available once it is part of an application.
    std::string_view componentName() const noexcept { return name_; }

protected:
    // The lifecycle hooks, in the order they run. Override the ones you need, and mark them
    // `override`: without it, a hook with a different signature (`void start(int)`)
    // compiles and is never called. (To let configuration turn a component off, derive
    // from keel::Conditional too.)

    /// Acquire and configure: open files, bind sockets, read settings. Runs after the
    /// init() of every dependency, on the thread that called App::init(), or on a thread of
    /// its own with keel::AsyncInit. Throw to abort startup. If it throws, neither stop() nor
    /// deinit() is called on this component: undo its partial work before throwing.
    virtual void init() {}
    /// Begin activity: accept connections, start threads. Runs after the init() of every
    /// component in the app, and after the start() of every dependency, on the thread that
    /// called App::start(), or on a thread of its own with keel::AsyncStart. Throw to abort
    /// startup. If it throws, deinit() is called on this component but stop() is not: undo
    /// its partial work before throwing (e.g. join a thread it launched).
    virtual void start() {}
    /// End activity: stop accepting, drain work, join the threads start() launched. Runs
    /// in reverse order, on the thread that calls App::stop(): the components that depend
    /// on this one are already stopped. Must not throw: handle or log failures
    /// here.
    virtual void stop() noexcept {}
    /// Release. Runs in reverse order, after every stop(), on the thread that calls
    /// App::deinit() (or destroys the App). Dependencies are still
    /// initialized, so they can still be used. Must not throw: handle or log cleanup
    /// failures here.
    virtual void deinit() noexcept {}

private:
    friend class Registry;
    template <class...>
    friend class keel::Component;

    // A stopped component is initialized again. A disabled one (a keel::Conditional whose
    // enabled() returned false) stays disabled.
    enum class State : unsigned char { created, initialized, started, deinitialized, disabled };

    // The dependency with this id, or nullptr if it isn't composed and not required.
    void* lookup(TypeId id, std::string_view dependency, bool required) const;
    // Every component bound to id, in init order, for getAll<I>(); throws unless this
    // component may use its dependencies now. Pass each one to useIfEnabled().
    const std::vector<Binding>& lookupAll(TypeId id, std::string_view dependency) const;
    // The bound object of an entry of lookupAll(), or nullptr if it is this component or
    // disabled; throws as use() does.
    void* useIfEnabled(const Binding& binding, std::string_view dependency) const;
    // Throws unless this component may use its dependencies now (from init() on).
    void checkCanUse(std::string_view accessor, std::string_view dependency) const;
    // The bound object, if its owner is initialized; else throws.
    void* use(const Binding& binding, std::string_view dependency) const;
    // Before init(): throws unless every required dependency resolves to one enabled
    // component, and every Optional<T> to at most one. (Only keel::Alternative<>s can fail
    // that: the rest is checked at compile time.)
    void checkDependencies() const;

    // Set when the component is added to an App, then only read.
    Registry* registry_ = nullptr;
    const Need* needs_ = nullptr;  // the entries of the keel::Component<...> list that init() checks
    std::size_t needCount_ = 0;
    std::string_view name_;
    const Conditional* conditional_ = nullptr;  // the component, if it is a keel::Conditional
    bool asyncInit_ = false;   // keel::AsyncInit
    bool asyncStart_ = false;  // keel::AsyncStart

    // Written only by the thread running this component's hook (or the rollback, after
    // every thread has finished). A dependent reads it after the scheduler has ordered that
    // write before the read (see Registry::Schedule). Atomic so that a lookup from a thread
    // the component started itself (in start(), say) isn't a data race.
    std::atomic<State> state_ = State::created;
};

}  // namespace detail

}  // namespace keel
