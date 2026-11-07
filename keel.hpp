// keel.hpp: application-level components with declared dependencies.
//
// Single header, C++20, no RTTI.
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include <array>
#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace keel {

/// Composition and usage errors: a dependency that isn't composed, a dependency used
/// before it is initialized, and so on.
class Error : public std::logic_error {
public:
    using std::logic_error::logic_error;
};

/// Marks a dependency in keel::Component<...> as optional: the component works without
/// it. If T is also in the app's keel::compose<...> list, keel constructs and initializes
/// it before this component (and deinitializes and destroys it after), and
/// findComponent<T>() returns it; otherwise findComponent<T>() returns nullptr. Use
/// findComponent<T>(), not getComponent<T>(). When T is composed, the dependency counts
/// like a required one: a cycle through it is a compile error.
template <class T>
struct Optional {};

/// Ordering only: if T is also in the app's keel::compose<...> list, this component is
/// constructed and initialized after T, and deinitialized and destroyed before it. It gives
/// no access to T; list T (or keel::Optional<T>) instead to use it. If T isn't composed,
/// nothing happens, and nothing reports it.
template <class T>
struct InitAfter {};

template <class... Dependencies>
class Component;

template <class... Components>
class App;

namespace detail {

// ===========================================================================
// Type utilities

template <class... Ts>
struct TypeList {};

template <class T, class List>
struct ContainsImpl;
template <class T, class... Ts>
struct ContainsImpl<T, TypeList<Ts...>> : std::bool_constant<(std::is_same_v<T, Ts> || ...)> {};
template <class T, class List>
inline constexpr bool contains = ContainsImpl<T, List>::value;

template <class... Ts>
struct UniqueImpl : std::true_type {};
template <class T, class... Ts>
struct UniqueImpl<T, Ts...> : std::bool_constant<!(std::is_same_v<T, Ts> || ...) && UniqueImpl<Ts...>::value> {};
template <class List>
struct UniqueList;
template <class... Ts>
struct UniqueList<TypeList<Ts...>> : UniqueImpl<Ts...> {};

template <class... Lists>
struct ConcatImpl {
    using type = TypeList<>;
};
template <class... As>
struct ConcatImpl<TypeList<As...>> {
    using type = TypeList<As...>;
};
template <class... As, class... Bs, class... Rest>
struct ConcatImpl<TypeList<As...>, TypeList<Bs...>, Rest...> : ConcatImpl<TypeList<As..., Bs...>, Rest...> {};
template <class... Lists>
using Concat = typename ConcatImpl<Lists...>::type;

template <class T>
inline constexpr bool alwaysFalse = false;

// The class of a pointer to member function: `R C::*` also matches `R (C::*)(Args...)`,
// with R deduced as the function type `R(Args...)`.
template <class M>
struct MemberOf;
template <class R, class C>
struct MemberOf<R C::*> {
    using type = C;
};

struct EmptySplit {
    using required = TypeList<>;
    using optional = TypeList<>;
    using initAfter = TypeList<>;
};

// Separates the entries of a keel::Component<...> list by kind: required, optional, and
// ordering only.
template <class D>
struct Split : EmptySplit {
    using required = TypeList<D>;
};
template <class T>
struct Split<Optional<T>> : EmptySplit {
    using optional = TypeList<T>;
};
template <class T>
struct Split<InitAfter<T>> : EmptySplit {
    using initAfter = TypeList<T>;
};

template <class T>
inline constexpr bool isDependencyMarker = false;
template <class T>
inline constexpr bool isDependencyMarker<Optional<T>> = true;
template <class T>
inline constexpr bool isDependencyMarker<InitAfter<T>> = true;

/// Human-readable type name at compile time, without RTTI.
template <class T>
constexpr std::string_view typeName() {
#if defined(__clang__) || defined(__GNUC__)
    // clang: "std::string_view keel::detail::typeName() [T = Logger]"
    // gcc:   "constexpr std::string_view keel::detail::typeName() [with T = Logger; ...]"
    constexpr std::string_view f = __PRETTY_FUNCTION__;
    constexpr auto begin = f.find("T = ") + 4;
    constexpr auto end = f.find_first_of(";]", begin);
    return f.substr(begin, end - begin);
#elif defined(_MSC_VER)
    // "... __cdecl keel::detail::typeName<class Logger>(void)"
    constexpr std::string_view f = __FUNCSIG__;
    constexpr auto begin = f.find("typeName<") + 9;
    constexpr auto end = f.rfind(">(void)");
    std::string_view name = f.substr(begin, end - begin);
    for (std::string_view prefix : {"class ", "struct ", "enum "}) {
        if (name.starts_with(prefix)) return name.substr(prefix.size());
    }
    return name;
#else
    return "<unknown type>";
#endif
}

using TypeId = const void*;

// Not const: identical read-only constants may be merged by the linker (MSVC /OPT:ICF),
// which would give two types the same id.
template <class T>
struct TypeIdTag {
    static inline char tag{};
};

/// A unique id per type without RTTI: the address of a per-type variable.
template <class T>
constexpr TypeId typeId() noexcept {
    return &TypeIdTag<T>::tag;
}

inline std::string concat(std::initializer_list<std::string_view> parts) {
    std::string out;
    for (std::string_view p : parts) out += p;
    return out;
}

class Registry;

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
    // compiles and is never called.

    /// Acquire and configure: open files, bind sockets, read settings. Runs after the
    /// init() of every dependency. Throw to abort startup. If it throws, neither stop() nor
    /// deinit() is called on this component: undo its partial work before throwing.
    virtual void init() {}
    /// Begin activity: accept connections, start threads. Runs after the init() of every
    /// component in the app, and after the start() of every dependency. Throw to abort
    /// startup. If it throws, deinit() is called on this component but stop() is not: undo
    /// its partial work before throwing (e.g. join a thread it launched).
    virtual void start() {}
    /// End activity: stop accepting, drain work. Runs in reverse order: the components
    /// that depend on this one are already stopped. Must not throw: handle or log failures
    /// here.
    virtual void stop() noexcept {}
    /// Release. Runs in reverse order, after every stop(). Dependencies are still
    /// initialized, so they can still be used. Must not throw: handle or log cleanup
    /// failures here.
    virtual void deinit() noexcept {}

private:
    friend class Registry;
    template <class...>
    friend class keel::Component;

    // A stopped component is initialized again.
    enum class State : unsigned char { created, initialized, started, deinitialized };

    // The dependency with this id, or nullptr if it isn't composed and not required.
    void* lookup(TypeId id, std::string_view dependency, bool required) const;

    Registry* registry_ = nullptr;
    std::string_view name_;
    State state_ = State::created;
};

// ===========================================================================
// Runtime state of an App: the components in init order, and lookups by type.

struct Binding {
    ComponentBase* owner;
    void* object;  // the component itself; cast back to its own type only
};

class Registry {
public:
    Registry() = default;
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    template <class C>
    void add(C& component) {
        ComponentBase& base = component;
        base.registry_ = this;
        base.name_ = typeName<C>();
        components_.push_back(&base);
        bindings_.emplace(typeId<C>(), Binding{&base, static_cast<void*>(&component)});
    }

    void init();
    void start();
    void stop() noexcept;
    void deinit() noexcept;

    /// init() was called: components may look up their dependencies.
    bool initCalled() const noexcept { return state_ != State::building; }

    const Binding* find(TypeId id) const {
        auto it = bindings_.find(id);
        return it == bindings_.end() ? nullptr : &it->second;
    }

private:
    // stopped vs initialized: the components are in the same state, but a stopped app
    // can't be started again.
    enum class State { building, initialized, started, stopped, failed, deinitialized };

    void stopAll() noexcept;
    void deinitAll() noexcept;

    std::vector<ComponentBase*> components_;  // in init order
    std::unordered_map<TypeId, Binding> bindings_;
    State state_ = State::building;
};

// ===========================================================================
// Storage of a composition

template <class C, class... Args>
C construct(Args&... args) {
    if constexpr (std::is_constructible_v<C, Args&...>) {
        return C(args...);
    } else if constexpr (std::is_default_constructible_v<C>) {
        return C();
    } else {
        static_assert(alwaysFalse<C>,
                      "keel: a component must be constructible from compose()'s arguments or default-constructible");
    }
}

template <class C>
struct Slot {
    template <class... Args>
    explicit Slot(Args&... args) : component(construct<C>(args...)) {}
    C component;
};

template <class List>
struct Storage;
template <class... Sorted>
struct Storage<TypeList<Sorted...>> : Slot<Sorted>... {
    // Bases are constructed in dependency order and destroyed in reverse.
    template <class... Args>
    explicit Storage(Args&... args) : Slot<Sorted>(args...)... {}

    template <class T>
    T& get() noexcept {
        return static_cast<Slot<T>&>(*this).component;
    }
};

// ===========================================================================
// Compile-time composition analysis for keel::compose<...>

// Diagnostics. Each failure is a class template named after the mistake, whose template
// arguments name the types involved: MissingDependency<Aggregator, WorkerPool>. A check
// that passes is an empty Passed<...> instead, so a failing one is never instantiated
// unless it is the mistake. The checks run per component (ComponentDiagnosis), which keeps
// the compiler's "in instantiation of" notes short. One mistake, one error.
template <class... Context>
struct Passed {};

template <class Component, class Dependency>
inline constexpr bool dependencyComposed = false;  // only read when the check fails
template <class Component, class Dependency>
struct MissingDependency {
    static_assert(dependencyComposed<Component, Dependency>,
                  "keel: missing dependency: the Component in MissingDependency<Component, Dependency> needs "
                  "Dependency, and it is not composed.");
};

// Optional<T> and InitAfter<T> may name a type that isn't composed, but it must be one that
// could be: a class, not const or volatile, not another marker. Anything else would never
// match a component.
template <class T>
inline constexpr bool couldBeComponent =
    std::is_class_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T> && !isDependencyMarker<T>;

template <class Component, class Entry>
inline constexpr bool validEntry = false;  // only read when the check fails
template <class Component, class Entry>
struct InvalidDependency {
    static_assert(validEntry<Component, Entry>,
                  "keel: the Component in InvalidDependency<Component, Entry> lists Entry (keel::Optional<T> or "
                  "keel::InitAfter<T>), and T can never be a component: T must be a class type, not "
                  "const/volatile, and not itself keel::Optional or keel::InitAfter");
};

// The check of an Optional<T> or InitAfter<T> entry: Passed, or InvalidDependency naming it.
template <class C, template <class> class Marker, class T>
using MarkerCheck =
    std::conditional_t<couldBeComponent<T>, Passed<C, Marker<T>>, InvalidDependency<C, Marker<T>>>;

template <class Cycle>
inline constexpr bool acyclic = false;
template <class Cycle>
struct DependencyCycle {
    static_assert(acyclic<Cycle>, "keel: dependency cycle among the components in DependencyCycle<TypeList<...>>");
};

// The init order of a dependency graph, computed at compile time. graph[i][j]: component
// i depends on component j.
template <std::size_t N>
using Graph = std::array<std::array<bool, N>, N>;

template <std::size_t N>
struct TopologicalOrder {
    std::array<std::size_t, N> index{};  // index[k]: the component placed k-th
    std::array<bool, N> placed{};
    std::size_t count = 0;  // < N if some components wait on each other (a cycle)
};

// Kahn's algorithm, always placing the earliest ready component of the compose<...> list,
// so components that don't depend on each other keep the list order. O(N²).
template <std::size_t N>
constexpr TopologicalOrder<N> topologicalOrder(const Graph<N>& graph) {
    TopologicalOrder<N> o;
    std::array<std::size_t, N> waitingFor{};  // dependencies not placed yet
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = 0; j < N; ++j) waitingFor[i] += graph[i][j] ? 1 : 0;
    }
    while (o.count < N) {
        std::size_t next = N;
        for (std::size_t i = 0; i < N && next == N; ++i) {
            if (!o.placed[i] && waitingFor[i] == 0) next = i;
        }
        if (next == N) break;  // the rest wait on each other
        o.placed[next] = true;
        o.index[o.count++] = next;
        for (std::size_t i = 0; i < N; ++i) {
            if (graph[i][next]) --waitingFor[i];
        }
    }
    return o;
}

// The components that are on a cycle: those that reach themselves through unplaced
// components. Components that only depend on a cycle are left out. Only evaluated when
// there is a cycle. O(N³), on the error path only.
template <std::size_t N>
constexpr std::array<bool, N> componentsOnCycles(const Graph<N>& graph, const std::array<bool, N>& placed) {
    Graph<N> reach{};
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = 0; j < N; ++j) reach[i][j] = graph[i][j] && !placed[i] && !placed[j];
    }
    for (std::size_t k = 0; k < N; ++k) {
        for (std::size_t i = 0; i < N; ++i) {
            if (!reach[i][k]) continue;
            for (std::size_t j = 0; j < N; ++j) reach[i][j] = reach[i][j] || reach[k][j];
        }
    }
    std::array<bool, N> out{};
    for (std::size_t i = 0; i < N; ++i) out[i] = reach[i][i];
    return out;
}

template <class List>
struct Composition;

template <class... Cs>
struct Composition<TypeList<Cs...>> {
    static constexpr std::size_t size = sizeof...(Cs);

    // Which components a dependency on T resolves to.
    template <class T>
    static constexpr std::array<bool, size> providers{std::is_same_v<T, Cs>...};

    template <class T>
    static constexpr std::size_t providerCount = [] {
        std::size_t n = 0;
        for (bool p : providers<T>) n += p ? 1 : 0;
        return n;
    }();

    // The check of one dependency: a Passed<...> if fine, else the named failure (see above).
    template <class C, class R>
    using RequiredCheck = std::conditional_t<(providerCount<R> > 0), Passed<C, R>, MissingDependency<C, R>>;

    template <class... Rs>
    static constexpr std::array<bool, size> edges(TypeList<Rs...>) {
        std::array<bool, size> row{};
        auto every = [&row](const std::array<bool, size>& all) {
            for (std::size_t i = 0; i < size; ++i) row[i] = row[i] || all[i];
        };
        (every(providers<Rs>), ...);
        return row;
    }

    // Every listed dependency that is composed orders like a required one.
    static constexpr Graph<size> graph{edges(typename Cs::DeclaredDependencies{})...};
    static constexpr TopologicalOrder<size> order = topologicalOrder(graph);
    static constexpr bool acyclic = order.count == size;
    static constexpr std::array<bool, size> onCycle =
        acyclic ? std::array<bool, size>{} : componentsOnCycles(graph, order.placed);

    template <std::size_t I>
    using At = std::tuple_element_t<I, std::tuple<Cs...>>;

    template <std::size_t... Is>
    static auto cycleOf(std::index_sequence<Is...>)
        -> Concat<std::conditional_t<onCycle[Is], TypeList<At<Is>>, TypeList<>>...>;
    using Cycle = decltype(cycleOf(std::make_index_sequence<size>{}));

    template <std::size_t... Is>
    static auto sortedOf(std::index_sequence<Is...>) -> TypeList<At<order.index[Is]>...>;
    using Sorted = std::conditional_t<acyclic, decltype(sortedOf(std::make_index_sequence<size>{})), TypeList<Cs...>>;
};

// All checks of one component C, as bases: only a failing one has a static_assert.
template <class Info, class C, class Required = typename C::RequiredDependencies,
          class Optionals = typename C::OptionalDependencies, class After = typename C::InitAfterDependencies>
struct ComponentDiagnosis;
template <class Info, class C, class... Rs, class... Os, class... As>
struct ComponentDiagnosis<Info, C, TypeList<Rs...>, TypeList<Os...>, TypeList<As...>>
    : Info::template RequiredCheck<C, Rs>..., MarkerCheck<C, Optional, Os>..., MarkerCheck<C, InitAfter, As>... {};

// Runs ComponentDiagnosis only on a well-formed list (naming it reads C's dependency
// list), and only for a component whose own list is valid (else that is the one error).
template <bool wellFormed, class Info, class C>
struct DiagnosisIf {
    using type = Passed<C>;
};
template <class Info, class C>
struct DiagnosisIf<true, Info, C> {
    using type = std::conditional_t<C::uniqueDependencies, ComponentDiagnosis<Info, C>, Passed<C>>;
};

// Every component's checks at once, so each mistake in each component is reported. The
// index keeps the bases distinct when the list itself is invalid.
template <bool wellFormed, class Info, class List, class Indices>
struct AllDiagnoses;
template <bool wellFormed, class Info, class... Cs, std::size_t... Is>
struct AllDiagnoses<wellFormed, Info, TypeList<Cs...>, std::index_sequence<Is...>>
    : std::conditional_t<wellFormed, typename DiagnosisIf<wellFormed, Info, Cs>::type,
                         Passed<Cs, std::integral_constant<std::size_t, Is>>>... {};

// Checks of the compose<...> list itself, each naming the offending type.
template <class T>
inline constexpr bool isComponentType = false;  // only read when the check fails
template <class T>
struct NotAComponent {
    static_assert(isComponentType<T>,
                  "keel: the T in NotAComponent<T> is in compose<...> but doesn't derive publicly from "
                  "keel::Component<...> (or isn't defined at this point)");
};

template <class T>
inline constexpr bool listedOnce = false;
template <class T>
struct DuplicateComponent {
    static_assert(listedOnce<T>, "keel: the T in DuplicateComponent<T> is listed more than once in compose<...>");
};

template <class T>
inline constexpr bool concreteType = false;
template <class T>
struct AbstractComponent {
    static_assert(concreteType<T>, "keel: the T in AbstractComponent<T> is abstract: compose concrete components");
};

// A pointer conversion checks access and ambiguity too, and works for incomplete types.
template <class T>
inline constexpr bool derivesFromComponent = std::is_convertible_v<T*, ComponentBase*>;
template <class T, bool component = derivesFromComponent<T>>
inline constexpr bool abstractComponent = false;
template <class T>
inline constexpr bool abstractComponent<T, true> = std::is_abstract_v<T>;

template <class... Cs>
struct ListInfo {
    static constexpr std::size_t size = sizeof...(Cs);
    static constexpr std::array<TypeId, size> ids{typeId<Cs>()...};

    // How many times the I-th type appears earlier in the list.
    template <std::size_t I>
    static constexpr std::size_t earlier = [] {
        std::size_t n = 0;
        for (std::size_t j = 0; j < I; ++j) n += ids[j] == ids[I] ? 1 : 0;
        return n;
    }();

    // One check per entry. A type listed twice is reported once, at its second entry.
    template <class C, std::size_t I>
    using Check = std::conditional_t<
        earlier<I> == 1, DuplicateComponent<C>,
        std::conditional_t<earlier<I> == 0 && !derivesFromComponent<C>, NotAComponent<C>,
                           std::conditional_t<earlier<I> == 0 && abstractComponent<C>, AbstractComponent<C>,
                                              Passed<C, std::integral_constant<std::size_t, I>>>>>;

    static constexpr bool ok = UniqueImpl<Cs...>::value && ((derivesFromComponent<Cs> && !abstractComponent<Cs>) && ...);
};

template <class Info, class Indices>
struct ListDiagnosis;
template <class... Cs, std::size_t... Is>
struct ListDiagnosis<ListInfo<Cs...>, std::index_sequence<Is...>>
    : ListInfo<Cs...>::template Check<Cs, Is>... {};

}  // namespace detail

// ===========================================================================
// keel::Component

/// Base class of every component. List the components it uses:
///   class Exporter : public keel::Component<Aggregator, Logger, keel::Optional<Metrics>,
///                                           keel::InitAfter<Migrations>>
/// The listed types may be incomplete (forward-declared) in the header.
template <class... Dependencies>
class Component : public detail::ComponentBase {
public:
    using RequiredDependencies = detail::Concat<typename detail::Split<Dependencies>::required...>;
    using OptionalDependencies = detail::Concat<typename detail::Split<Dependencies>::optional...>;
    using InitAfterDependencies = detail::Concat<typename detail::Split<Dependencies>::initAfter...>;
    using DeclaredDependencies = detail::Concat<RequiredDependencies, OptionalDependencies, InitAfterDependencies>;

    // T, Optional<T> and InitAfter<T> all name T, so listing two of them is a duplicate.
    static constexpr bool uniqueDependencies = detail::UniqueList<DeclaredDependencies>::value;

    // Checked here rather than in the class body: a failed check there would make the
    // derived class invalid, and every later use of it would add an error.
    Component() noexcept {
        static_assert(uniqueDependencies,
                      "keel: a dependency is listed twice in keel::Component<...> (T, keel::Optional<T> and "
                      "keel::InitAfter<T> count as the same)");
    }

protected:
    /// A required dependency. Compiles only if T is listed in keel::Component<...> as a
    /// required dependency (not keel::Optional<T> or keel::InitAfter<T>). Usable from init()
    /// on; resolve it there and keep the reference.
    template <class T>
    T& getComponent() const {
        // At most one of these fails. With a duplicate in the list, none does: the duplicate
        // is the one error (see the constructor).
        if constexpr (uniqueDependencies) {
            static_assert(!detail::contains<T, OptionalDependencies>,
                          "keel: T is declared keel::Optional<T>; use findComponent<T>(), which may return nullptr");
            static_assert(!detail::contains<T, InitAfterDependencies>,
                          "keel: T is declared keel::InitAfter<T>, which is ordering only; replace it with T to use T");
            static_assert(detail::contains<T, DeclaredDependencies>,
                          "keel: getComponent<T>() requires T to be listed in this component's keel::Component<...>");
        }
        return *static_cast<T*>(lookup(detail::typeId<T>(), detail::typeName<T>(), true));
    }

    /// An optional dependency: nullptr if T is not in the app's keel::compose<...> list.
    /// Compiles only if keel::Optional<T> is listed in keel::Component<...>. Usable from
    /// init() on (throws keel::Error before, e.g. from a constructor); resolve it there and
    /// keep the pointer.
    template <class T>
    T* findComponent() const {
        if constexpr (uniqueDependencies) {  // as in getComponent()
            static_assert(!detail::contains<T, RequiredDependencies>,
                          "keel: T is a required dependency; use getComponent<T>(), which never returns nullptr");
            static_assert(!detail::contains<T, InitAfterDependencies>,
                          "keel: T is declared keel::InitAfter<T>, which is ordering only; replace it with "
                          "keel::Optional<T> to use T");
            static_assert(detail::contains<T, DeclaredDependencies>,
                          "keel: findComponent<T>() requires keel::Optional<T> in this component's "
                          "keel::Component<...>");
        }
        return static_cast<T*>(lookup(detail::typeId<T>(), detail::typeName<T>(), false));
    }
};

// ===========================================================================
// keel::App and keel::compose

namespace detail {

template <class List>
class AppImpl;

// What run(fn) returns after a mistake in fn: it converts to anything and supports the
// usual operators, so that in conversion contexts such as `return app.run(...);` the
// mistake is the only error. Never defined: such code never compiles.
struct RunMistake {
    template <class T>
    operator T() const;

#define KEEL_RUN_MISTAKE_OPERATOR(op)                         \
    template <class T>                                        \
    friend RunMistake operator op(const RunMistake&, const T&); \
    template <class T>                                        \
    friend RunMistake operator op(const T&, const RunMistake&);
    KEEL_RUN_MISTAKE_OPERATOR(==)
    KEEL_RUN_MISTAKE_OPERATOR(!=)
    KEEL_RUN_MISTAKE_OPERATOR(<)
    KEEL_RUN_MISTAKE_OPERATOR(>)
    KEEL_RUN_MISTAKE_OPERATOR(<=)
    KEEL_RUN_MISTAKE_OPERATOR(>=)
    KEEL_RUN_MISTAKE_OPERATOR(+)
    KEEL_RUN_MISTAKE_OPERATOR(-)
    KEEL_RUN_MISTAKE_OPERATOR(*)
    KEEL_RUN_MISTAKE_OPERATOR(/)
    KEEL_RUN_MISTAKE_OPERATOR(%)
#undef KEEL_RUN_MISTAKE_OPERATOR
};

// Diagnostics of run(fn), each named after the mistake.
template <class C, class MemberFunction>
struct RunTargetNotComposed : RunMistake {
    static_assert(alwaysFalse<C>,
                  "keel: run(&C::f) calls f on the component C, and the C in RunTargetNotComposed<C, F> is not in "
                  "this App's compose<...> list (a member function inherited from a base class names the base)");
};
template <class MemberFunction>
struct RunTargetNotCallable : RunMistake {
    static_assert(alwaysFalse<MemberFunction>,
                  "keel: run(&C::f) calls f on the component (an lvalue C&) with no arguments, and the F in "
                  "RunTargetNotCallable<F> can't be called that way");
};
template <class Fn>
struct RunNotCallable : RunMistake {
    static_assert(alwaysFalse<Fn>,
                  "keel: run(fn) needs fn callable with no arguments or with the App (App&), or a member function "
                  "of a component (&C::f); the Fn in RunNotCallable<Fn> is neither");
};
template <class T>
struct NotAnApp : RunMistake {
    static_assert(alwaysFalse<T>, "keel: keel::run<AppType>(...) needs a keel::App<...>; the T in NotAnApp<T> isn't one");
};

// A class whose operator() is a single non-template function (not a generic lambda).
template <class F>
inline constexpr bool hasPlainCallOperator = requires { &F::operator(); };


// The composition is checked when the class is instantiated. The graph analysis only runs
// on a list of distinct, concrete components, so a list that isn't one reports that and
// nothing else.
template <class... Cs>
class AppImpl<TypeList<Cs...>> {
    using List = ListInfo<Cs...>;
    static constexpr bool wellFormed = List::ok;
    using Info = Composition<std::conditional_t<wellFormed, TypeList<Cs...>, TypeList<>>>;
    using Indices = std::index_sequence_for<Cs...>;

    static_assert(sizeof...(Cs) > 0, "keel: compose<...> needs at least one component");
    static_assert(sizeof(ListDiagnosis<List, Indices>) > 0);  // names a non-component or a duplicate
    static_assert(sizeof(AllDiagnoses<wellFormed, Info, TypeList<Cs...>, Indices>) > 0);  // names missing dependencies
    static_assert(sizeof(std::conditional_t<Info::acyclic, Passed<>, DependencyCycle<typename Info::Cycle>>) > 0);

public:
    template <class... Args>
    explicit AppImpl(Args&&... args) : storage_(args...) {
        addAll(typename Info::Sorted{});  // in dependency order
    }

    // Not copyable or movable: components hold pointers to each other. (Without the
    // deleted move, the constructor template above would be picked for a move.)
    AppImpl(const AppImpl&) = delete;
    AppImpl(AppImpl&&) = delete;
    AppImpl& operator=(const AppImpl&) = delete;
    AppImpl& operator=(AppImpl&&) = delete;

    ~AppImpl() { registry_.deinit(); }

    /// Calls init() on every component, in dependency order. If one throws, the components
    /// already initialized are deinitialized in reverse, and the exception propagates.
    /// Callable once.
    void init() { registry_.init(); }
    /// Calls start() on every component, in dependency order. Requires a successful init().
    /// If one throws, the components already started are stopped in reverse, then every
    /// component is deinitialized in reverse, and the exception propagates. Callable once.
    void start() { registry_.start(); }
    /// Calls stop() on every started component, in reverse order. Does nothing if the app
    /// isn't started.
    void stop() noexcept { registry_.stop(); }
    /// Stops the app if it is started, then calls deinit() on every component, in reverse
    /// order. Does nothing the second time. The destructor calls it too.
    void deinit() noexcept { registry_.deinit(); }

    /// For the composition root.
    template <class T>
    T& getComponent() {
        static_assert(contains<T, TypeList<Cs...>>, "keel: T is not a component of this App");
        return storage_.template get<T>();
    }

protected:
    // init, start, fn, then stop and deinit, also if fn throws. Self is the App, which
    // run(fn) passes to fn.
    template <class Self, class Fn>
    static decltype(auto) runWith(Self& self, Fn&& fn) {
        Registry& registry = static_cast<AppImpl&>(self).registry_;
        registry.init();
        registry.start();
        struct Shutdown {
            Registry& registry;
            ~Shutdown() { registry.deinit(); }  // stops first
        } shutdown{registry};
        return call(self, std::forward<Fn>(fn));
    }

    // fn(self), fn(), or a member function of one of the components (&Server::serve).
    // A mistake gives exactly one error: one of the diagnostics above, or the compiler's
    // own error inside a generic callable.
    template <class Self, class Fn>
    static decltype(auto) call([[maybe_unused]] Self& self, Fn&& fn) {
        using F = std::remove_cvref_t<Fn>;
        if constexpr (std::is_member_function_pointer_v<F>) {
            using C = typename MemberOf<F>::type;
            if constexpr (!contains<C, TypeList<Cs...>>) {
                return RunTargetNotComposed<C, F>{};
            } else if constexpr (!std::is_invocable_v<F, C&>) {
                return RunTargetNotCallable<F>{};
            } else {
                return (self.template getComponent<C>().*fn)();
            }
        } else if constexpr (std::is_invocable_v<Fn>) {
            // Callable with no arguments. If it also takes the App, it gets the App.
            if constexpr (std::is_invocable_v<Fn, Self&>) {
                return std::forward<Fn>(fn)(self);
            } else {
                return std::forward<Fn>(fn)();
            }
        } else if constexpr (std::is_class_v<F> && !hasPlainCallOperator<F>) {
            // A generic lambda (or an overloaded function object): called with the App
            // directly, so an error in its body is reported once, by the compiler, instead
            // of turning into "not callable".
            return std::forward<Fn>(fn)(self);
        } else if constexpr (std::is_invocable_v<Fn, Self&>) {
            return std::forward<Fn>(fn)(self);
        } else {
            return RunNotCallable<Fn>{};
        }
    }

private:
    template <class... Sorted>
    void addAll(TypeList<Sorted...>) {
        (registry_.add(storage_.template get<Sorted>()), ...);
    }

    Registry registry_;  // declared first: outlives the components
    Storage<typename Info::Sorted> storage_;
};

}  // namespace detail

/// A statically composed application. Components in any order: keel checks the
/// composition at compile time and derives the order from the declared dependencies.
/// Drive it with run(fn), or step by step: init(), start(), ..., stop(), deinit().
template <class... Components>
class App : public detail::AppImpl<detail::TypeList<Components...>> {
    using Base = detail::AppImpl<detail::TypeList<Components...>>;

public:
    using Base::Base;

    /// Runs the whole lifecycle: init(), start(), then fn, then stop() and deinit(), also
    /// if fn throws. Returns what fn returns. fn is one of:
    ///   app.run([] { waitForShutdown(); });          // a callable with no arguments
    ///   app.run([](ServerApp& running) { ... });     // a callable taking the App
    ///   app.run(&Server::serve);                     // a member function of a component,
    ///                                                // called on that component
    /// A callable that accepts both gets the App. Name the App type to write the parameter
    /// without templates:
    ///   using ServerApp = keel::App<Server, Config>;
    ///   ServerApp app;
    ///   app.run([](ServerApp& running) { running.getComponent<Server>().serve(); });
    /// If init() or start() throws, the exception propagates after the rollback, and fn
    /// doesn't run. Calling run() after init() throws keel::Error. A reference returned by fn
    /// refers to an object that is already deinitialized when run() returns.
    template <class Fn>
    decltype(auto) run(Fn&& fn) {
        return Base::runWith(*this, std::forward<Fn>(fn));
    }
    // So that run(&C::f) picks the overload of f that takes no arguments.
    template <class R, class C>
    decltype(auto) run(R (C::*fn)()) {
        return Base::runWith(*this, fn);
    }
    template <class R, class C>
    decltype(auto) run(R (C::*fn)() const) {
        return Base::runWith(*this, fn);
    }
};

namespace detail {
template <class T>
inline constexpr bool isApp = false;
template <class... Cs>
inline constexpr bool isApp<App<Cs...>> = true;
}  // namespace detail

/// `auto app = keel::compose<Settings, Console, Greeter>(argc, argv);`
/// Each component constructible from the arguments receives them; the others are
/// default-constructed.
template <class... Components, class... Args>
App<Components...> compose(Args&&... args) {
    return App<Components...>(std::forward<Args>(args)...);
}

namespace detail {
template <class AppType, class Fn, class... Args>
auto runApp(Fn&& fn, Args&&... args) {
    if constexpr (!isApp<AppType>) {
        return NotAnApp<AppType>{};
    } else {
        AppType app(std::forward<Args>(args)...);
        return app.run(std::forward<Fn>(fn));
    }
}
}  // namespace detail

/// Constructs AppType from args and runs it (see App::run), e.g. when main() picks one of
/// several compositions: `return keel::run<ServerApp>(&Server::serve, argc, argv);`
/// Returns what fn returns, by value: the app is gone by then, so a reference result is
/// copied.
template <class AppType, class Fn, class... Args>
auto run(Fn&& fn, Args&&... args) {
    return detail::runApp<AppType>(std::forward<Fn>(fn), std::forward<Args>(args)...);
}
// So that run(&C::f) picks the overload of f that takes no arguments.
template <class AppType, class R, class C, class... Args>
auto run(R (C::*fn)(), Args&&... args) {
    return detail::runApp<AppType>(fn, std::forward<Args>(args)...);
}
template <class AppType, class R, class C, class... Args>
auto run(R (C::*fn)() const, Args&&... args) {
    return detail::runApp<AppType>(fn, std::forward<Args>(args)...);
}

// ===========================================================================
// Implementation

namespace detail {

inline void* ComponentBase::lookup(TypeId id, std::string_view dependency, bool required) const {
    if (registry_ == nullptr) {
        throw Error(concat({required ? "getComponent<" : "findComponent<", dependency,
                            ">() called before the component was part of an application (from its constructor?). "
                            "Dependencies are available from init() on."}));
    }
    // The caller's own lifecycle first, so a misplaced lookup fails whether or not an
    // optional dependency happens to be composed.
    if (!registry_->initCalled()) throw Error(concat({name_, " used ", dependency, " before init()"}));
    if (state_ == State::deinitialized) throw Error(concat({name_, " used ", dependency, " after its deinit()"}));
    const Binding* b = registry_->find(id);
    if (b == nullptr) {
        if (!required) return nullptr;
        throw Error(concat({name_, " uses ", dependency, ", which is not part of the application"}));
    }
    switch (b->owner->state_) {
        case State::initialized:
        case State::started:
            return b->object;
        case State::created:
            throw Error(concat({name_, " used ", dependency, " before it was initialized"}));
        case State::deinitialized:
            break;
    }
    throw Error(concat({name_, " used ", dependency, " after it was deinitialized"}));
}

// Stops every started component, in reverse order.
inline void Registry::stopAll() noexcept {
    for (auto it = components_.rbegin(); it != components_.rend(); ++it) {
        ComponentBase& c = **it;
        if (c.state_ != ComponentBase::State::started) continue;
        c.stop();
        c.state_ = ComponentBase::State::initialized;
    }
}

// Deinitializes every initialized component, in reverse order.
inline void Registry::deinitAll() noexcept {
    for (auto it = components_.rbegin(); it != components_.rend(); ++it) {
        ComponentBase& c = **it;
        if (c.state_ != ComponentBase::State::initialized) continue;
        c.deinit();
        c.state_ = ComponentBase::State::deinitialized;
    }
}

inline void Registry::init() {
    if (state_ != State::building) throw Error("keel: init() can be called only once per App");
    state_ = State::failed;  // until proven otherwise
    try {
        for (ComponentBase* c : components_) {
            c->init();
            c->state_ = ComponentBase::State::initialized;
        }
    } catch (...) {
        deinitAll();  // roll back what was initialized, then report the init() failure
        throw;
    }
    state_ = State::initialized;
}

inline void Registry::start() {
    switch (state_) {
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
        for (ComponentBase* c : components_) {
            c->start();
            c->state_ = ComponentBase::State::started;
        }
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
