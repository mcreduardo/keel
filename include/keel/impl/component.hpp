// keel/impl/component.hpp: keel::Component<...>, the base class of every component: its
// accessors getComponent<T>(), findComponent<T>() and getAll<I>(), and the lifecycle hooks
// (init(), start(), stop(), deinit()) of its type-erased base, detail::ComponentBase.
//
// Part of keel.hpp: tools/amalgamate.py joins the parts into the single header.
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include "markers.hpp"
#include "type_list.hpp"
#include "type_name.hpp"
#include "lists.hpp"
#include "diagnostics.hpp"

#include <atomic>
#include <cstddef>
#include <string_view>
#include <vector>

namespace keel {

// Declared for ComponentBase's friend declaration.
template <class... Dependencies>
class Component;

namespace detail {

class Registry;
class ComponentBase;
struct Need;

// What a lookup finds (see Registry). Defined here, before keel::Component<...>, whose
// getAll<I>() reads them.
struct Binding {
    ComponentBase* owner;
    void* object;      // the component, or its interface subobject; cast back to the bound type only
    bool alternative;  // bound through keel::Alternative<>: chosen at runtime
};

// Defined in diagnostics.hpp; keel::Component<...>'s constructor instantiates it.
template <class Base>
struct ComponentListCheck;

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

// ===========================================================================
// keel::Component

/// Base class of every component. List the components and interfaces it uses: T,
/// keel::Optional<T>, keel::After<T>, keel::All<I>, keel::Optional<keel::All<I>>. Only
/// dependencies; what the component is goes in its other base classes (keel::Provides<I>,
/// keel::Alternative<I>, keel::Conditional, keel::AsyncInit, keel::AsyncStart):
///   class Exporter : public keel::Component<Aggregator, Logger, keel::Optional<Metrics>,
///                                           keel::After<Migrations>, keel::All<Format>>,
///                    public keel::Provides<Sink>,
///                    public Sink
/// A dependency is a component, or an interface that one component provides (derives from
/// keel::Provides<>) or that keel::Alternative<> components provide (keel::All<I>: every
/// component that provides I). The listed types may be incomplete (forward-declared) in the
/// header.
template <class... Dependencies>
class Component : public detail::ComponentBase {
public:
    using RequiredDependencies = detail::Concat<typename detail::Split<Dependencies>::required...>;
    using OptionalDependencies = detail::Concat<typename detail::Split<Dependencies>::optional...>;
    using AfterDependencies = detail::Concat<typename detail::Split<Dependencies>::after...>;
    using AllDependencies = detail::Concat<typename detail::Split<Dependencies>::all...>;  // also Optional<All<I>>
    using MisnestedDependencies = detail::Concat<typename detail::Split<Dependencies>::misnested...>;
    using DeclaredDependencies = detail::Concat<RequiredDependencies, OptionalDependencies, AfterDependencies,
                                                AllDependencies, MisnestedDependencies>;
    // keel::Provides<I> and keel::Alternative<I> are base classes, not entries: listed, they
    // are ProvidesAsDependency and AlternativeAsDependency.
    using ListedProvides = detail::Concat<typename detail::Split<Dependencies>::listedProvides...>;
    using ListedAlternatives = detail::Concat<typename detail::Split<Dependencies>::listedAlternatives...>;
    using Entries = detail::TypeList<Dependencies...>;  // as written

    // T, Optional<T>, After<T>, All<T> and Optional<All<T>> all name T, so listing two
    // of them is a duplicate.
    static constexpr bool uniqueDependencies = detail::UniqueList<DeclaredDependencies>::value;
    static constexpr bool uniqueListedProvides = detail::UniqueList<ListedProvides>::value;
    static constexpr bool uniqueListedAlternatives = detail::UniqueList<ListedAlternatives>::value;
    // keel::AsyncInit and keel::AsyncStart are base classes: listed, they are AsyncAsDependency.
    using ListedAsync = detail::Concat<typename detail::Split<Dependencies>::listedAsync...>;
    static constexpr bool uniqueListedAsync = detail::UniqueList<ListedAsync>::value;
    static constexpr bool uniqueEntries =
        uniqueDependencies && uniqueListedProvides && uniqueListedAlternatives && uniqueListedAsync;

private:
    // The accessor asserts on T run unless the list has a mistake that is the one error
    // instead: a duplicate (see the constructor) or keel::All<keel::Optional<T>>
    // (MisnestedAll, at compose time).
    template <class T>
    static constexpr bool checkedAccess = uniqueDependencies && !detail::contains<T, MisnestedDependencies>;
    // A lookup of keel::AsyncInit or keel::AsyncStart is an error, unless the list names it
    // (AsyncAsDependency, or MisplacedAsync inside another entry), which is then the one
    // error, at compose time.
    template <class T>
    static constexpr bool markerAccess =
        detail::contains<T, DeclaredDependencies> || detail::contains<T, ListedAsync>;

public:

    // Checked here rather than in the class body: a failed check there would make the
    // derived class invalid, and every later use of it would add an error.
    Component() noexcept { static_assert(sizeof(detail::ComponentListCheck<Component>) > 0); }

protected:
    /// A required dependency. Compiles only if T is listed in keel::Component<...> as a
    /// required dependency (not keel::Optional<T>, keel::After<T> or keel::All<T>).
    /// Usable from init() on (and from enabled(), for a keel::Conditional); resolve it in
    /// init() and keep the reference. With keel::AsyncInit too: T's init() has finished, and
    /// what it did is visible. For an interface with keel::Alternative<> providers, the
    /// enabled one.
    template <class T>
    T& getComponent() const {
        // At most one of these fails, and none if the list itself is wrong (checkedAccess).
        if constexpr (detail::isAsyncMarker<T>) {
            static_assert(markerAccess<T>,
                          "keel: keel::AsyncInit and keel::AsyncStart are base classes, not dependencies: there is no "
                          "component to get");
        } else if constexpr (checkedAccess<T>) {
            static_assert(!detail::contains<T, OptionalDependencies>,
                          "keel: T is declared keel::Optional<T>; use findComponent<T>(), which may return nullptr");
            static_assert(!detail::contains<T, AfterDependencies>,
                          "keel: T is declared keel::After<T>, which is ordering only; replace it with T to use T");
            static_assert(!detail::contains<T, AllDependencies>,
                          "keel: T is declared keel::All<T> or keel::Optional<keel::All<T>>; use getAll<T>()");
            static_assert(detail::contains<T, DeclaredDependencies>,
                          "keel: getComponent<T>() requires T to be listed in this component's keel::Component<...>");
        }
        return *static_cast<T*>(lookup(detail::typeId<T>(), detail::typeName<T>(), true));
    }

    /// An optional dependency: nullptr if T is not in the app's keel::compose<...> list, or
    /// is a keel::Conditional that is disabled. Compiles only if keel::Optional<T> is listed
    /// in keel::Component<...>. Usable from init() on, and from enabled() (throws
    /// keel::Error before, e.g. from a constructor); resolve it in init() and keep the
    /// pointer.
    template <class T>
    T* findComponent() const {
        if constexpr (detail::isAsyncMarker<T>) {  // as in getComponent()
            static_assert(markerAccess<T>,
                          "keel: keel::AsyncInit and keel::AsyncStart are base classes, not dependencies: there is no "
                          "component to find");
        } else if constexpr (checkedAccess<T>) {
            static_assert(!detail::contains<T, RequiredDependencies>,
                          "keel: T is a required dependency; use getComponent<T>(), which never returns nullptr");
            static_assert(!detail::contains<T, AfterDependencies>,
                          "keel: T is declared keel::After<T>, which is ordering only; replace it with "
                          "keel::Optional<T> to use T");
            static_assert(!detail::contains<T, AllDependencies>,
                          "keel: T is declared keel::All<T> or keel::Optional<keel::All<T>>; use getAll<T>()");
            static_assert(detail::contains<T, DeclaredDependencies>,
                          "keel: findComponent<T>() requires keel::Optional<T> in this component's "
                          "keel::Component<...>");
        }
        return static_cast<T*>(lookup(detail::typeId<T>(), detail::typeName<T>(), false));
    }

    /// Every enabled component that provides I (derives from keel::Provides<I>, lists
    /// keel::Alternative<I>, or is I itself), except this one, in init order (each after the
    /// ones it depends on): a keel::Conditional that is disabled is left out. Never empty for
    /// keel::All<I> (some provider isn't conditional); possibly empty for
    /// keel::Optional<keel::All<I>>. Compiles only if one of the two is listed in
    /// keel::Component<...>. Usable from init() on, and from enabled() (throws keel::Error
    /// before); resolve it in init() and keep the vector:
    ///   for (HealthCheck* check : getAll<HealthCheck>()) { ... }
    template <class I>
    std::vector<I*> getAll() const {
        if constexpr (detail::isAsyncMarker<I>) {  // as in getComponent()
            static_assert(markerAccess<I>,
                          "keel: keel::AsyncInit and keel::AsyncStart are base classes, not dependencies: there are no "
                          "components to get");
        } else if constexpr (checkedAccess<I>) {
            static_assert(!detail::contains<I, RequiredDependencies>,
                          "keel: I is a required dependency (one provider); use getComponent<I>(), or replace it "
                          "with keel::All<I> to use every provider");
            static_assert(!detail::contains<I, OptionalDependencies>,
                          "keel: I is declared keel::Optional<I> (at most one provider); use findComponent<I>(), or "
                          "replace it with keel::Optional<keel::All<I>> to use every provider");
            static_assert(!detail::contains<I, AfterDependencies>,
                          "keel: I is declared keel::After<I>, which is ordering only; replace it with "
                          "keel::All<I> (or keel::Optional<keel::All<I>>) to use I");
            static_assert(detail::contains<I, DeclaredDependencies>,
                          "keel: getAll<I>() requires keel::All<I> or keel::Optional<keel::All<I>> in this "
                          "component's keel::Component<...>");
        }
        constexpr std::string_view name = detail::typeName<I>();
        const std::vector<detail::Binding>& bindings = lookupAll(detail::typeId<I>(), name);
        std::vector<I*> out;
        out.reserve(bindings.size());  // the one allocation, if any provider exists
        for (const detail::Binding& b : bindings) {
            if (void* p = useIfEnabled(b, name)) out.push_back(static_cast<I*>(p));
        }
        return out;
    }
};

}  // namespace keel
