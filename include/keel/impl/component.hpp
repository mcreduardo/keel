// keel::Component<...>, the base class of every component: accessors and lifecycle hooks.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include "markers.hpp"
#include "type_list.hpp"
#include "type_name.hpp"
#include "lists.hpp"
#include "diagnostics.hpp"

#include <atomic>
#include <span>
#include <string_view>
#include <vector>

namespace keel {

// Forward-declared for ComponentBase's friend declaration.
template <class... Dependencies>
class Component;

namespace detail {

class Registry;
class ComponentBase;
struct Need;

// What a lookup finds. Defined before keel::Component<...>, whose getAll<I>() reads it.
struct Binding {
    ComponentBase* owner;
    void* object;      // the component, or its interface subobject; cast back to the bound type only
    bool alternative;  // bound through keel::Alternative<>: chosen at runtime
};

// Defined in diagnostics.hpp; keel::Component<...>'s constructor instantiates it.
template <class Base>
struct ComponentListCheck;

class ComponentBase {
public:
    ComponentBase() = default;
    ComponentBase(const ComponentBase&) = delete;
    ComponentBase& operator=(const ComponentBase&) = delete;
    virtual ~ComponentBase() = default;

    /// The component's type name, available once it is part of an application.
    std::string_view componentName() const noexcept { return name_; }

protected:
    /// Acquire and configure; runs after the init() of every dependency. May throw.
    virtual void init() {}
    /// Begin activity; runs after every component's init(). May throw.
    virtual void start() {}
    /// End activity; runs in reverse order.
    virtual void stop() noexcept {}
    /// Release; runs in reverse order, after every stop().
    virtual void deinit() noexcept {}

private:
    friend class Registry;
    template <class...>
    friend class keel::Component;

    // A stopped component is initialized again; a disabled one stays disabled.
    enum class State : unsigned char { created, initialized, started, deinitialized, disabled };

    void* lookup(TypeId id, std::string_view dependency, bool required) const;
    // Every component bound to id, in init order; pass each one to useIfEnabled().
    const std::vector<Binding>& lookupAll(TypeId id, std::string_view dependency) const;
    // nullptr if the binding is this component or disabled.
    void* useIfEnabled(const Binding& binding, std::string_view dependency) const;
    void checkCanUse(std::string_view accessor, std::string_view dependency) const;
    void* use(const Binding& binding, std::string_view dependency) const;
    // Before init(): only keel::Alternative<>s can fail this; the rest is checked at compile time.
    void checkDependencies() const;

    Registry* registry_ = nullptr;
    std::span<const Need> needs_;  // the entries of the keel::Component<...> list that init() checks
    std::string_view name_;
    const Conditional* conditional_ = nullptr;  // the component, if it is a keel::Conditional
    bool asyncInit_ = false;
    bool asyncStart_ = false;

    // Written only by the thread running this component's hook; ordered by Registry::Schedule.
    // Atomic so a lookup from a thread the component started itself isn't a data race.
    std::atomic<State> state_ = State::created;
};

}  // namespace detail

/// Base class of every component; lists its dependencies (see docs/dependencies.md).
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
    using ListedProvides = detail::Concat<typename detail::Split<Dependencies>::listedProvides...>;
    using ListedAlternatives = detail::Concat<typename detail::Split<Dependencies>::listedAlternatives...>;
    using Entries = detail::TypeList<Dependencies...>;  // as written

    // T, Optional<T>, After<T>, All<T> and Optional<All<T>> all name T: two of them are a duplicate.
    static constexpr bool uniqueDependencies = detail::UniqueList<DeclaredDependencies>::value;
    static constexpr bool uniqueListedProvides = detail::UniqueList<ListedProvides>::value;
    static constexpr bool uniqueListedAlternatives = detail::UniqueList<ListedAlternatives>::value;
    using ListedAsync = detail::Concat<typename detail::Split<Dependencies>::listedAsync...>;
    static constexpr bool uniqueListedAsync = detail::UniqueList<ListedAsync>::value;
    static constexpr bool uniqueEntries =
        uniqueDependencies && uniqueListedProvides && uniqueListedAlternatives && uniqueListedAsync;

private:
    // Accessor asserts are skipped when the list itself is wrong, so one mistake gives one error.
    template <class T>
    static constexpr bool checkedAccess = uniqueDependencies && !detail::contains<T, MisnestedDependencies>;
    // A lookup of an async marker asserts unless the list names it; then AsyncAsDependency is the one error.
    template <class T>
    static constexpr bool markerAccess =
        detail::contains<T, DeclaredDependencies> || detail::contains<T, ListedAsync>;

public:

    // Checked here, not in the class body: a failed check there would make every later use an error.
    Component() noexcept { static_assert(sizeof(detail::ComponentListCheck<Component>) > 0); }

protected:
    /// A required dependency, usable from init() (or enabled()) on.
    template <class T>
    T& getComponent() const {
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

    /// An optional dependency, or nullptr if not composed or disabled.
    template <class T>
    T* findComponent() const {
        if constexpr (detail::isAsyncMarker<T>) {
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

    /// Every enabled provider of I except this component, in init order.
    template <class I>
    std::vector<I*> getAll() const {
        if constexpr (detail::isAsyncMarker<I>) {
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
