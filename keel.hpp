// keel.hpp: application-level components with declared dependencies.
//
// Single header, C++20, no RTTI.
// Generated from include/keel/ by tools/amalgamate.py; do not edit.
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <initializer_list>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace keel {

/// Composition and usage errors reported at runtime.
class Error : public std::logic_error {
public:
    using std::logic_error::logic_error;
};

/// Optional dependency: findComponent<T>() returns it, or nullptr if absent or disabled.
template <class T>
struct Optional {};

/// Ordering-only dependency: init after T if T is composed; gives no access to T.
template <class T>
struct After {};

/// Base class: this component implements the interface I (derive from I publicly too).
template <class I>
struct Provides;  // defined in lists.hpp because of physical design limitations

/// Base class: implements I as one of several implementations chosen at runtime.
template <class I>
struct Alternative;  // defined in lists.hpp because of physical design limitations

/// Dependency on every provider of the interface I, in init order.
template <class I>
struct All {};

/// Base class that runs this component's init() (and enabled()) on a thread of its own.
struct AsyncInit {};

/// Base class that runs this component's start() on a thread of its own.
struct AsyncStart {};

/// A named group of components, listed in compose<...> or App<...> in place of them.
template <class... Components>
struct Bundle {};

// Forward-declared for Conditional's friend declaration.
namespace detail {
class Registry;
}

/// Base class of a component that configuration may turn off by overriding enabled().
class Conditional {
protected:
    virtual bool enabled() const = 0;

    Conditional() = default;
    Conditional(const Conditional&) = default;
    Conditional& operator=(const Conditional&) = default;
    virtual ~Conditional() = default;

private:
    friend class detail::Registry;
};

/// A lifecycle hook of a component, as recorded in keel::Timing.
enum class Phase { init, start, stop, deinit };

/// The name of a phase: "init", "start", "stop" or "deinit".
constexpr std::string_view toString(Phase phase) noexcept {
    switch (phase) {
        case Phase::init:
            return "init";
        case Phase::start:
            return "start";
        case Phase::stop:
            return "stop";
        case Phase::deinit:
            return "deinit";
    }
    return "?";
}

/// How long one lifecycle call of one component took; an entry of App::timings().
struct Timing {
    std::string_view component;            // the component's name, as in App::initOrder()
    Phase phase = Phase::init;
    std::chrono::nanoseconds duration{};   // measured with std::chrono::steady_clock
    bool failed = false;                   // the call threw (init() or start()); the rollback follows
};

namespace detail {
// Defined in type_name.hpp, which the single header places after this part.
template <class T>
constexpr std::string_view typeName();
}  // namespace detail

/// The name keel uses for T, as the compiler spells it.
template <class T>
constexpr std::string_view typeName() noexcept {
    return detail::typeName<T>();
}

namespace detail {

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

template <class List, class T>
struct AppendImpl;
template <class... Ts, class T>
struct AppendImpl<TypeList<Ts...>, T> {
    using type = TypeList<Ts..., T>;
};
template <class List, class T>
using Append = typename AppendImpl<List, T>::type;

// The first type of a TypeList, or Fallback if it is empty.
template <class List, class Fallback>
struct FirstOrImpl {
    using type = Fallback;
};
template <class T, class... Ts, class Fallback>
struct FirstOrImpl<TypeList<T, Ts...>, Fallback> {
    using type = T;
};
template <class List, class Fallback>
using FirstOr = typename FirstOrImpl<List, Fallback>::type;

// `R C::*` also matches `R (C::*)(Args...)`, with R deduced as the function type.
template <class M>
struct MemberOf;
template <class R, class C>
struct MemberOf<R C::*> {
    using type = C;
};

// Class templates at namespace scope, not members of Composition: MSVC doesn't expand a pack
// inside a member function's return type there.
template <class List, auto Keep, class Sequence>
struct KeepIf;
template <class... Ts, auto Keep, std::size_t... Is>
struct KeepIf<TypeList<Ts...>, Keep, std::index_sequence<Is...>> {
    using type = Concat<std::conditional_t<Keep[Is], TypeList<Ts>, TypeList<>>...>;
};

template <class List, auto Index, class Sequence>
struct Reorder;
template <class... Ts, auto Index, std::size_t... Is>
struct Reorder<TypeList<Ts...>, Index, std::index_sequence<Is...>> {
    using type = TypeList<std::tuple_element_t<Index[Is], std::tuple<Ts...>>...>;
};

// A class whose operator() is a single non-template function (not a generic lambda).
template <class F>
inline constexpr bool hasPlainCallOperator = requires { &F::operator(); };

/// Human-readable type name at compile time, without RTTI.
template <class T>
constexpr std::string_view typeName() {
#if defined(__clang__)
    // "... typeName() [T = Logger]": ends at the last ']' (the name may contain ']' and ';').
    constexpr std::string_view f = __PRETTY_FUNCTION__;
    constexpr auto begin = f.find("T = ") + 4;
    return f.substr(begin, f.rfind(']') - begin);
#elif defined(__GNUC__)
    // "... typeName() [with T = Logger; std::string_view = ...]": ends where the typedef is listed,
    // searched from the end (the name may contain ';' and ']'), else at the last ']'.
    constexpr std::string_view f = __PRETTY_FUNCTION__;
    constexpr auto begin = f.find("T = ") + 4;
    constexpr auto typedefs = f.rfind("; std::string_view = ");
    constexpr auto end = typedefs != std::string_view::npos && typedefs > begin ? typedefs : f.rfind(']');
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

// Not const: the linker may merge identical read-only constants (MSVC /OPT:ICF), giving two
// types the same id.
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

// How an entry uses the type it names. The listed* and misnested kinds are mistakes,
// reported at compose time.
enum class Use {
    required,
    optional,
    ordering,
    listedProvides,
    listedAlternative,
    listedAsync,
    all,
    optionalAll,
    misnested
};

struct EmptySplit {
    using required = TypeList<>;
    using optional = TypeList<>;
    using after = TypeList<>;
    using all = TypeList<>;
    using misnested = TypeList<>;
    using listedProvides = TypeList<>;
    using listedAlternatives = TypeList<>;
    using listedAsync = TypeList<>;
};

// Separates the entries of a keel::Component<...> list by kind. `target` is the type the entry names.
template <class D>
struct Split : EmptySplit {
    using required = TypeList<D>;
    using target = D;
    static constexpr Use use = Use::required;
};
template <class T>
struct Split<Optional<T>> : EmptySplit {
    using optional = TypeList<T>;
    using target = T;
    static constexpr Use use = Use::optional;
};
template <class T>
struct Split<After<T>> : EmptySplit {
    using after = TypeList<T>;
    using target = T;
    static constexpr Use use = Use::ordering;
};
template <class I>
struct Split<Provides<I>> : EmptySplit {
    using listedProvides = TypeList<I>;
    using target = I;
    static constexpr Use use = Use::listedProvides;
};
template <class I>
struct Split<Alternative<I>> : EmptySplit {
    using listedAlternatives = TypeList<I>;
    using target = I;
    static constexpr Use use = Use::listedAlternative;
};
template <class I>
struct Split<All<I>> : EmptySplit {
    using all = TypeList<I>;
    using target = I;
    static constexpr Use use = Use::all;
};
template <class I>
struct Split<Optional<All<I>>> : EmptySplit {
    using all = TypeList<I>;
    using target = I;
    static constexpr Use use = Use::optionalAll;
};
// Accessors accept I for it, so MisnestedAll is the one error.
template <class I>
struct Split<All<Optional<I>>> : EmptySplit {
    using misnested = TypeList<I>;
    using target = I;
    static constexpr Use use = Use::misnested;
};

template <>
struct Split<AsyncInit> : EmptySplit {
    using listedAsync = TypeList<AsyncInit>;
    using target = AsyncInit;
    static constexpr Use use = Use::listedAsync;
};
template <>
struct Split<AsyncStart> : EmptySplit {
    using listedAsync = TypeList<AsyncStart>;
    using target = AsyncStart;
    static constexpr Use use = Use::listedAsync;
};

template <class T>
inline constexpr bool isAsyncMarker = std::is_same_v<T, AsyncInit> || std::is_same_v<T, AsyncStart>;

template <class T>
inline constexpr bool isDependencyMarker = isAsyncMarker<T>;
template <class T>
inline constexpr bool isDependencyMarker<Optional<T>> = true;
template <class T>
inline constexpr bool isDependencyMarker<After<T>> = true;
template <class T>
inline constexpr bool isDependencyMarker<Provides<T>> = true;
template <class T>
inline constexpr bool isDependencyMarker<Alternative<T>> = true;
template <class T>
inline constexpr bool isDependencyMarker<All<T>> = true;

// A type an entry may name: a class, not cv-qualified, not a marker. Anything else would never match.
template <class T>
inline constexpr bool couldBeComponent =
    std::is_class_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T> && !isDependencyMarker<T>;

template <class C>
inline constexpr bool isBundle = false;
template <class... Cs>
inline constexpr bool isBundle<Bundle<Cs...>> = true;

// A repeated bundle is expanded only the first time, so it is one DuplicateBundle<B>, not a
// duplicate of each of its components.
template <class Components, class RepeatedBundles = TypeList<>>
struct Expanded {
    using components = Components;
    using repeated = RepeatedBundles;
};

// Left to right, in place: a bundle's components take its position.
template <class Seen, class Repeated, class Out, class... Rest>
struct Expand : Expanded<Out, Repeated> {};
template <bool bundle, class Seen, class Repeated, class Out, class First, class... Rest>
struct ExpandFirst : Expand<Seen, Repeated, Append<Out, First>, Rest...> {};
template <class Seen, class Repeated, class Out, class First, class... Rest>
struct Expand<Seen, Repeated, Out, First, Rest...>
    : ExpandFirst<isBundle<First>, Seen, Repeated, Out, First, Rest...> {};
template <class Seen, class Repeated, class Out, class... Inner, class... Rest>
struct ExpandFirst<true, Seen, Repeated, Out, Bundle<Inner...>, Rest...>
    : std::conditional_t<
          contains<Bundle<Inner...>, Seen>,
          Expand<Seen,
                 std::conditional_t<contains<Bundle<Inner...>, Repeated>, Repeated, Append<Repeated, Bundle<Inner...>>>,
                 Out, Rest...>,
          Expand<Append<Seen, Bundle<Inner...>>, Repeated, Out, Inner..., Rest...>> {};

template <bool anyBundle, class... Cs>
struct ExpandImpl {
    using type = Expanded<TypeList<Cs...>>;
};
template <class... Cs>
struct ExpandImpl<true, Cs...> {
    using type = Expanded<typename Expand<TypeList<>, TypeList<>, TypeList<>, Cs...>::components,
                          typename Expand<TypeList<>, TypeList<>, TypeList<>, Cs...>::repeated>;
};
template <class... Cs>
using ExpandBundles = typename ExpandImpl<(isBundle<Cs> || ...), Cs...>::type;

}  // namespace detail

// Defined here, after couldBeComponent. I is checked in the constructor, not the class body:
// a failed check there would make every later use of the component an error.
template <class I>
struct Provides {
    constexpr Provides() noexcept {
        static_assert(detail::couldBeComponent<I>,
                      "keel: keel::Provides<I> needs an interface I that a dependency could name: a class type, "
                      "not const or volatile, not a reference, and not a keel marker (keel::Optional, keel::All, "
                      "...). Write keel::Provides<Storage>, not keel::Provides<const Storage>.");
    }
};

namespace detail {
// Lets keel know a component is an alternative of something without knowing I.
struct AlternativeMarker {};
}  // namespace detail

// Defined here, after couldBeComponent; checked as keel::Provides<I> is.
template <class I>
struct Alternative : detail::AlternativeMarker {
    constexpr Alternative() noexcept {
        static_assert(detail::couldBeComponent<I>,
                      "keel: keel::Alternative<I> needs an interface I that a dependency could name: a class type, "
                      "not const or volatile, not a reference, and not a keel marker (keel::Optional, keel::All, "
                      "...). Write keel::Alternative<Cache>, not keel::Alternative<const Cache>.");
    }
};

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

namespace detail {

// Deduced, so that members of C named like Component's own (Entries, ...) don't hide them.
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
// Provides<I>/Alternative<I> listed by mistake count as provided, so the mistake is the one error.
template <class C>
using ListedOf = typename ComponentInfo<C>::Listed;
template <class C>
using ListedAlternativesOf = typename ComponentInfo<C>::Alternatives;
template <class C>
inline constexpr bool uniqueEntriesOf = ComponentOf<C>::uniqueEntries;

// An entry init() checks: a required or Optional<T> dependency, which may resolve to alternatives.
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
template <class C>
using Needs = NeedsTable<typename NeedsImpl<EntriesOf<C>>::Checked>;

// Public and once (else InaccessibleConditional at compose time). False for an incomplete C.
template <class C>
inline constexpr bool isConditional = std::is_convertible_v<C*, Conditional*>;

// Public and once (else InaccessibleAsync at compose time).
template <class C>
inline constexpr bool hasAsyncInit = std::is_convertible_v<C*, AsyncInit*>;
template <class C>
inline constexpr bool hasAsyncStart = std::is_convertible_v<C*, AsyncStart*>;

// The pointer conversion checks access and ambiguity, and is false for an incomplete I.
template <class C, class I>
constexpr bool implements() {
    if constexpr (couldBeComponent<I>) {
        return std::is_convertible_v<C*, I*>;
    } else {
        return false;
    }
}

// The pointer conversion checks access and ambiguity. An incomplete T is not a component.
template <class T>
constexpr bool derivesFromComponentImpl() {
    if constexpr (std::is_class_v<T>) {
        return std::is_convertible_v<T*, ComponentBase*> && requires { componentBaseOf(static_cast<T*>(nullptr)); };
    } else {
        return false;
    }
}
template <class T>
inline constexpr bool derivesFromComponent = derivesFromComponentImpl<T>();

// A failure is instantiated only if it is the mistake: one mistake, one error. compose<...>
// derives from the outcomes, isValid<...> only tests isPassed<>, so the two can't disagree.
// Each failure asserts a variable template that is false and read only when the check fails.
template <class... Context>
struct Passed {};

template <class Outcome>
inline constexpr bool isPassed = false;
template <class... Context>
inline constexpr bool isPassed<Passed<Context...>> = true;

// Carried as a type: returning the failure by value would instantiate it.
template <class Result>
using Outcome = std::type_identity<Result>;

// The component's constructor and compose<...> share this instantiation: a duplicate is one error.
template <class Base>
struct ComponentListCheck {
    static_assert(Base::uniqueDependencies,
                  "keel: a dependency is listed twice in keel::Component<...> (T, keel::Optional<T>, "
                  "keel::After<T>, keel::All<T> and keel::Optional<keel::All<T>> count as the same)");
    static_assert(Base::uniqueListedAlternatives,
                  "keel: keel::Alternative<I> is listed twice in keel::Component<...>, which lists only what the "
                  "component needs. Inherit keel::Alternative<I> instead, once: class C : public "
                  "keel::Component<...>, public keel::Alternative<I>, public I, public keel::Conditional");
    static_assert(Base::uniqueListedProvides,
                  "keel: keel::Provides<I> is listed twice in keel::Component<...>, which lists only what the "
                  "component needs. Inherit keel::Provides<I> instead, once: class C : public "
                  "keel::Component<...>, public keel::Provides<I>, public I");
    static_assert(Base::uniqueListedAsync,
                  "keel: keel::AsyncInit or keel::AsyncStart is listed twice in keel::Component<...>, which lists "
                  "only dependencies. Inherit it instead, once: class C : public keel::Component<...>, public "
                  "keel::AsyncInit");
};

template <class Component, class Dependency>
inline constexpr bool dependencyComposed = false;
template <class Component, class Dependency>
struct MissingDependency {
    static_assert(dependencyComposed<Component, Dependency>,
                  "keel: missing dependency: the Component in MissingDependency<Component, Dependency> needs "
                  "Dependency, and no composed component is or provides it.");
};

template <class Component, class Interface, class Providers>
inline constexpr bool singleProvider = false;
template <class Component, class Interface, class Providers>
struct AmbiguousDependency {
    static_assert(singleProvider<Component, Interface, Providers>,
                  "keel: ambiguous dependency: the Component in AmbiguousDependency<Component, Interface, "
                  "TypeList<Providers...>> needs one Interface, and each of the Providers is or provides it. "
                  "Compose only one of them, depend on the one you want by its own type, depend on "
                  "keel::All<Interface> to use all of them, or let configuration choose: derive "
                  "each of them from keel::Alternative<Interface> instead of keel::Provides<Interface>, and from "
                  "keel::Conditional, and enable one at runtime.");
};

template <class Interface, class Provides, class Alternatives>
inline constexpr bool unmixedProviders = false;
template <class Interface, class Provides, class Alternatives>
struct MixedProviders {
    static_assert(unmixedProviders<Interface, Provides, Alternatives>,
                  "keel: something needs one Interface, and its providers in MixedProviders<Interface, "
                  "TypeList<Provides...>, TypeList<Alternatives...>> are mixed: the Provides are Interface or derive "
                  "from keel::Provides<Interface>, the Alternatives derive from keel::Alternative<Interface>. "
                  "Derive each of them from keel::Alternative<Interface> (instead of keel::Provides<Interface>) and "
                  "from keel::Conditional, to choose one at runtime, or compose only one.");
};

template <class Component, class Interface>
inline constexpr bool anyProvider = false;
template <class Component, class Interface>
struct MissingProvider {
    static_assert(anyProvider<Component, Interface>,
                  "keel: the Component in MissingProvider<Component, Interface> lists keel::All<Interface>, which "
                  "needs at least one provider, and no composed component (other than Component itself) is or "
                  "provides Interface. Compose one, or depend on keel::Optional<keel::All<Interface>> if none is "
                  "fine.");
};

template <class Component, class Interface, class Providers>
inline constexpr bool unconditionalProvider = false;
template <class Component, class Interface, class Providers>
struct OnlyConditionalProviders {
    static_assert(unconditionalProvider<Component, Interface, Providers>,
                  "keel: the Component in OnlyConditionalProviders<Component, Interface, TypeList<Providers...>> "
                  "lists keel::All<Interface>, which is never empty, and each of the Providers of Interface is a "
                  "keel::Conditional that configuration may turn off. Depend on "
                  "keel::Optional<keel::All<Interface>> if none is fine, or compose a provider that isn't "
                  "conditional.");
};

template <class Component, class T>
inline constexpr bool unconditionalDependency = false;
template <class Component, class T>
struct RequiredConditional {
    static_assert(unconditionalDependency<Component, T>,
                  "keel: the Component in RequiredConditional<Component, T> requires T, and T is a "
                  "keel::Conditional that configuration may turn off. Depend on keel::Optional<T> instead, and "
                  "use findComponent<T>(), which returns nullptr while T is disabled (or keel::After<T> for "
                  "ordering only).");
};

template <class Component, class Interface, class Provider>
inline constexpr bool unconditionalInterface = false;
template <class Component, class Interface, class Provider>
struct RequiredConditionalProvider {
    static_assert(unconditionalInterface<Component, Interface, Provider>,
                  "keel: the Component in RequiredConditionalProvider<Component, Interface, Provider> requires "
                  "Interface, and its one provider, Provider, is a keel::Conditional that configuration may turn "
                  "off. Depend on keel::Optional<Interface> instead, or compose a provider that is always there "
                  "(to choose one of several at runtime, make each a keel::Alternative<Interface> and a "
                  "keel::Conditional).");
};

template <class Component>
inline constexpr bool conditionalAlternative = false;
template <class Component>
struct AlternativeNotConditional {
    static_assert(conditionalAlternative<Component>,
                  "keel: the Component in AlternativeNotConditional<Component> derives from keel::Alternative<I>, "
                  "so configuration decides whether it is the one, but it doesn't derive from keel::Conditional. "
                  "Derive from keel::Conditional publicly and override `bool enabled() const`, or derive from "
                  "keel::Provides<I> instead if it is always the one.");
};

template <class Component, class Interface>
inline constexpr bool providesOrAlternative = false;
template <class Component, class Interface>
struct ProvidesAndAlternative {
    static_assert(providesOrAlternative<Component, Interface>,
                  "keel: the Component in ProvidesAndAlternative<Component, Interface> derives from "
                  "keel::Provides<Interface> and from keel::Alternative<Interface>. Keep one: "
                  "keel::Alternative<Interface> if configuration chooses it among other implementations, "
                  "keel::Provides<Interface> if it is always the one.");
};

template <class Component, class Interface>
inline constexpr bool wellNestedAll = false;
template <class Component, class Interface>
struct MisnestedAll {
    static_assert(wellNestedAll<Component, Interface>,
                  "keel: the Component in MisnestedAll<Component, Interface> lists "
                  "keel::All<keel::Optional<Interface>>; write keel::Optional<keel::All<Interface>> for a list "
                  "that may be empty");
};

template <class Component, class T, class Derived>
inline constexpr bool providerDeclared = false;
template <class Component, class T, class Derived>
struct UndeclaredProvider {
    static_assert(providerDeclared<Component, T, Derived>,
                  "keel: the Component in UndeclaredProvider<Component, T, Derived> depends on T, nothing "
                  "composed is or provides T, and Derived derives from T without deriving from keel::Provides<T>. "
                  "Dependencies match by exact type: derive Derived from keel::Provides<T> too (publicly), or "
                  "depend on Derived.");
};

template <class Component, class T, class Derived>
inline constexpr bool subclassDeclared = false;
template <class Component, class T, class Derived>
struct UndeclaredSubclass {
    static_assert(subclassDeclared<Component, T, Derived>,
                  "keel: the Component in UndeclaredSubclass<Component, T, Derived> depends on T, which isn't "
                  "composed. Derived is a subclass of the component T, but keel matches exact types: compose T "
                  "itself, or derive T from keel::Provides<T> (publicly) so its subclasses provide T.");
};

template <class Component, class T, class Derived>
inline constexpr bool providerListed = false;
template <class Component, class T, class Derived>
struct LeftOutOfAll {
    static_assert(providerListed<Component, T, Derived>,
                  "keel: the Component in LeftOutOfAll<Component, T, Derived> lists keel::All<T> (or "
                  "keel::Optional<keel::All<T>>), and Derived derives from T but would be left out: keel matches "
                  "exact types. Derive Derived from keel::Provides<T> too (for a subclass of the component T, "
                  "derive T itself from it), or stop deriving from T.");
};

template <class Component, class Interface>
inline constexpr bool derivesFromInterface = false;
template <class Component, class Interface>
struct NotDerivedFromInterface {
    static_assert(derivesFromInterface<Component, Interface>,
                  "keel: the Component in NotDerivedFromInterface<Component, Interface> derives from "
                  "keel::Provides<Interface> (or keel::Alternative<Interface>) but doesn't derive from "
                  "Interface publicly and unambiguously (or Interface isn't defined at this point)");
};

template <class Component, class Interface>
inline constexpr bool accessibleProvides = false;
template <class Component, class Interface>
struct InaccessibleProvides {
    static_assert(accessibleProvides<Component, Interface>,
                  "keel: the Component in InaccessibleProvides<Component, Interface> derives from "
                  "keel::Provides<Interface> privately, protectedly or more than once, so keel can't see that it "
                  "provides Interface. Derive from keel::Provides<Interface> publicly, once (a subclass of a "
                  "provider inherits it): class C : public keel::Component<...>, public keel::Provides<Interface>, "
                  "public Interface");
};

template <class Component, class Interface>
inline constexpr bool accessibleAlternative = false;
template <class Component, class Interface>
struct InaccessibleAlternative {
    static_assert(accessibleAlternative<Component, Interface>,
                  "keel: the Component in InaccessibleAlternative<Component, Interface> derives from "
                  "keel::Alternative<Interface> privately, protectedly or more than once, so keel can't see that it "
                  "is an alternative of Interface. Derive from keel::Alternative<Interface> publicly, once: class C "
                  ": public keel::Component<...>, public keel::Alternative<Interface>, public Interface, public "
                  "keel::Conditional");
};

template <class Component, class Interface>
inline constexpr bool notListedAlternative = false;
template <class Component, class Interface>
struct AlternativeAsDependency {
    static_assert(notListedAlternative<Component, Interface>,
                  "keel: the Component in AlternativeAsDependency<Component, Interface> lists "
                  "keel::Alternative<Interface> in its keel::Component<...>, which lists only what the component "
                  "needs. Inherit it instead, next to keel::Component<...>: class Component : public "
                  "keel::Component<...>, public keel::Alternative<Interface>, public Interface, public "
                  "keel::Conditional");
};

template <class Component, class Interface>
inline constexpr bool notListedProvides = false;
template <class Component, class Interface>
struct ProvidesAsDependency {
    static_assert(notListedProvides<Component, Interface>,
                  "keel: the Component in ProvidesAsDependency<Component, Interface> lists keel::Provides<Interface> "
                  "in its keel::Component<...>, which lists only what the component needs. Inherit it instead, next "
                  "to keel::Component<...>: class Component : public keel::Component<...>, public "
                  "keel::Provides<Interface>, public Interface");
};

// One error per mistaken provider of one interface.
template <class... Failures>
struct ProviderFailures : Failures... {};

template <class Component, class Entry>
inline constexpr bool validEntry = false;
template <class Component, class Entry>
struct InvalidDependency {
    static_assert(validEntry<Component, Entry>,
                  "keel: the Component in InvalidDependency<Component, Entry> lists Entry (T, keel::Optional<T>, "
                  "keel::After<T> or keel::All<T>), and T can never be a component or an interface: T must be "
                  "a class type, not a reference, not const/volatile, and not itself keel::Optional, "
                  "keel::After, keel::Provides, keel::Alternative or keel::All");
};

template <class Component, class Entry>
inline constexpr bool asyncMarkerOnItsOwn = false;
template <class Component, class Entry>
struct MisplacedAsync {
    static_assert(asyncMarkerOnItsOwn<Component, Entry>,
                  "keel: the Component in MisplacedAsync<Component, Entry> lists keel::AsyncInit or "
                  "keel::AsyncStart inside Entry. They aren't dependencies but base classes: class C : public "
                  "keel::Component<Config>, public keel::AsyncInit");
};

template <class Component, class Marker>
inline constexpr bool asyncNotListed = false;
template <class Component, class Marker>
struct AsyncAsDependency {
    static_assert(asyncNotListed<Component, Marker>,
                  "keel: the Component in AsyncAsDependency<Component, Marker> lists the Marker (keel::AsyncInit or "
                  "keel::AsyncStart) in its keel::Component<...>, which lists only dependencies. Inherit it "
                  "instead, next to keel::Component<...>: class Component : public keel::Component<...>, public "
                  "Marker");
};

template <class Component, class Bundle>
inline constexpr bool notABundle = false;
template <class Component, class Bundle>
struct BundleAsDependency {
    static_assert(notABundle<Component, Bundle>,
                  "keel: the Component in BundleAsDependency<Component, Bundle> lists a keel::Bundle<...> in its "
                  "keel::Component<...> (as T, keel::Optional<T>, keel::After<T>, ...). Bundles only group "
                  "components for compose<...>: list the components you use, each on its own.");
};

template <class Bundle>
inline constexpr bool bundleOnce = false;
template <class Bundle>
struct DuplicateBundle {
    static_assert(bundleOnce<Bundle>,
                  "keel: the Bundle in DuplicateBundle<Bundle> is reached more than once in compose<...> (listed "
                  "twice, or also inside another bundle). List it once.");
};

template <class Cycle>
inline constexpr bool acyclic = false;
template <class Cycle>
struct DependencyCycle {
    static_assert(acyclic<Cycle>, "keel: dependency cycle among the components in DependencyCycle<TypeList<...>>");
};

template <class T>
inline constexpr bool isComponentType = false;
template <class T>
struct NotAComponent {
    static_assert(isComponentType<T>,
                  "keel: the T in NotAComponent<T> is in compose<...> but doesn't derive publicly, and once, "
                  "from keel::Component<...> (or isn't defined at this point). A keel::Bundle<...> is expanded "
                  "only when listed as is: not const, and not through a class derived from it");
};

template <class T>
inline constexpr bool listedOnce = false;
template <class T>
struct DuplicateComponent {
    static_assert(listedOnce<T>,
                  "keel: the T in DuplicateComponent<T> is listed more than once in compose<...> (counting the "
                  "components of each keel::Bundle<...>: two bundles may both contain T)");
};

template <class T>
inline constexpr bool concreteType = false;
template <class T>
struct AbstractComponent {
    static_assert(concreteType<T>,
                  "keel: the T in AbstractComponent<T> is abstract: compose concrete components. (A "
                  "keel::Conditional must override `bool enabled() const`, const included.)");
};

template <class T>
inline constexpr bool accessibleConditional = false;
template <class T>
struct InaccessibleConditional {
    static_assert(accessibleConditional<T>,
                  "keel: the T in InaccessibleConditional<T> derives from keel::Conditional privately, protectedly "
                  "or more than once, so keel can't ask its enabled(). Derive from keel::Conditional publicly, "
                  "once.");
};

// The empty pack only makes the assertion depend on a template argument.
template <class... None>
struct EmptyComposition {
    static_assert(sizeof...(None) > 0, "keel: compose<...> needs at least one component");
};

template <class T>
inline constexpr bool accessibleAsync = false;
template <class T>
struct InaccessibleAsync {
    static_assert(accessibleAsync<T>,
                  "keel: the T in InaccessibleAsync<T> derives from keel::AsyncInit or keel::AsyncStart privately, "
                  "protectedly or more than once, so keel can't see it. Derive from it publicly, once: class C : "
                  "public keel::Component<...>, public keel::AsyncInit");
};

// Converts to anything and supports the usual operators, so the mistake in fn is the only
// error, e.g. in `return app.run(...);`. Never defined.
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

template <class T>
struct NotInApp {
    static_assert(alwaysFalse<T>,
                  "keel: getComponent<T>(): the T in NotInApp<T> is neither a component of this App nor provided "
                  "by one. Keel matches exact types: a component that derives from T must also derive from "
                  "keel::Provides<T> (for a subclass of a component T, T itself must)");
};
template <class Interface, class Providers>
struct AmbiguousInterface {
    static_assert(alwaysFalse<Interface>,
                  "keel: getComponent<I>(): each of the Providers in AmbiguousInterface<I, TypeList<Providers...>> "
                  "provides I: get a specific one by its own type (if I itself is one of them, compose only one), "
                  "or make each of them a keel::Alternative<I> and a keel::Conditional");
};

template <class C, class MemberFunction>
struct RunTargetNotComposed : RunMistake {
    static_assert(alwaysFalse<C>,
                  "keel: run(&C::f) calls f on the component C (or on the one component that provides C), and "
                  "the C in RunTargetNotComposed<C, F> is neither in this App's compose<...> list nor provided by "
                  "one of its components. Keel matches exact types, and a member function inherited from a base "
                  "class names the base: derive the component from keel::Provides<Base> too, or call it from a "
                  "lambda");
};
template <class Interface, class Providers>
struct RunTargetAmbiguous : RunMistake {
    static_assert(alwaysFalse<Interface>,
                  "keel: run(&I::f) calls f on the one component that provides I, and each of the Providers in "
                  "RunTargetAmbiguous<I, TypeList<Providers...>> provides it: call it from a lambda on the one "
                  "you want, or make each of them a keel::Alternative<I> and a keel::Conditional");
};
// As a base, the composition mistake run(&C::f) runs into is reported once with the others.
template <class Mistake>
struct RunTargetMistake : RunMistake, Mistake {};
template <class C, class Provider>
struct RunTargetConditional : RunMistake {
    static_assert(alwaysFalse<C>,
                  "keel: run(&C::f) calls f on the one component that is or provides C, and the Provider in "
                  "RunTargetConditional<C, Provider> is a keel::Conditional that configuration may turn off. "
                  "Call it from a lambda that checks first (app.getComponent<C>() throws keel::Error while it "
                  "is disabled), or run a member function of a component that is always there");
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

// graph[i][j]: component i depends on component j.
template <std::size_t N>
using Graph = std::array<std::array<bool, N>, N>;

template <std::size_t N>
struct TopologicalOrder {
    std::array<std::size_t, N> index{};  // index[k]: the component placed k-th
    std::array<bool, N> placed{};
    std::size_t count = 0;  // < N if some components wait on each other (a cycle)
};

// Kahn's algorithm, always placing the earliest ready component, so independent components
// keep the compose<...> order. O(N²).
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

// Iterative depth-first searches, forward or backward; each row is scanned once overall: O(N²).
template <std::size_t N>
struct CycleSearch {
    constexpr CycleSearch(const Graph<N>& g, const std::array<bool, N>& p, bool f) : graph(g), placed(p), forward(f) {}

    const Graph<N>& graph;
    const std::array<bool, N>& placed;
    bool forward;
    std::array<std::size_t, N> mark{};  // the search that reached it, from 1; 0: none yet
    std::array<std::size_t, N> finished{};
    std::size_t finishedCount = 0;
    std::size_t searches = 0;

    constexpr bool edge(std::size_t from, std::size_t to) const {
        return forward ? graph[from][to] : graph[to][from];
    }
    constexpr void search(std::size_t root) {
        std::array<std::size_t, N> next{};  // the next column to scan in each row
        std::array<std::size_t, N> stack{};
        std::size_t depth = 0;
        stack[depth++] = root;
        mark[root] = ++searches;
        while (depth > 0) {
            const std::size_t v = stack[depth - 1];
            std::size_t& j = next[v];
            while (j < N && (placed[j] || mark[j] != 0 || !edge(v, j))) ++j;
            if (j < N) {
                mark[j] = searches;
                stack[depth++] = j;
            } else {
                finished[finishedCount++] = v;
                --depth;
            }
        }
    }
};

// Kosaraju's algorithm, O(N²): unplaced components in an SCC of two or more, or self-dependent.
// Components that only depend on a cycle are left out. Only evaluated when there is a cycle.
template <std::size_t N>
constexpr std::array<bool, N> componentsOnCycles(const Graph<N>& graph, const std::array<bool, N>& placed) {
    CycleSearch<N> first(graph, placed, true);
    for (std::size_t i = 0; i < N; ++i) {
        if (!placed[i] && first.mark[i] == 0) first.search(i);
    }
    // Backward searches, latest finished first; each reaches exactly one SCC.
    CycleSearch<N> second(graph, placed, false);
    for (std::size_t k = first.finishedCount; k-- > 0;) {
        if (second.mark[first.finished[k]] == 0) second.search(first.finished[k]);
    }
    std::array<std::size_t, N + 1> size{};
    for (std::size_t i = 0; i < N; ++i) ++size[second.mark[i]];  // mark 0: placed
    std::array<bool, N> out{};
    for (std::size_t i = 0; i < N; ++i) out[i] = !placed[i] && (size[second.mark[i]] > 1 || graph[i][i]);
    return out;
}

// Asked for each I something refers to, since C++20 can't list a class's bases. Traits only:
// evaluated for every component and referred type. The mistakes come last (`>= notDerived`).
enum class Provision : unsigned char { none, declared, notDerived, inaccessible };
template <class C, class I, bool interface = couldBeComponent<I>>
inline constexpr Provision provision = Provision::none;
template <class C, class I>
inline constexpr Provision provision<C, I, true> =
    std::is_convertible_v<C*, Provides<I>*>
        ? (std::is_convertible_v<C*, I*> ? Provision::declared : Provision::notDerived)
        : (std::is_base_of_v<Provides<I>, C> ? Provision::inaccessible : Provision::none);

// The same for keel::Alternative<I>.
template <class C, class I, bool interface = couldBeComponent<I>>
inline constexpr Provision alternativeProvision = Provision::none;
template <class C, class I>
inline constexpr Provision alternativeProvision<C, I, true> =
    std::is_convertible_v<C*, Alternative<I>*>
        ? (std::is_convertible_v<C*, I*> ? Provision::declared : Provision::notDerived)
        : (std::is_base_of_v<Alternative<I>, C> ? Provision::inaccessible : Provision::none);

template <class C, class I>
inline constexpr bool providesBase = provision<C, I> == Provision::declared || provision<C, I> == Provision::notDerived;
template <class C, class I>
inline constexpr bool alternativeBase =
    alternativeProvision<C, I> == Provision::declared || alternativeProvision<C, I> == Provision::notDerived;

// Reported per mistaken provider, the first that applies.
template <class C, class I>
inline constexpr bool brokenProvider = provision<C, I> >= Provision::notDerived ||
                                       alternativeProvision<C, I> >= Provision::notDerived ||
                                       (providesBase<C, I> && alternativeBase<C, I>);
template <class C, class I>
using ProviderFailure = std::conditional_t<
    provision<C, I> == Provision::inaccessible, InaccessibleProvides<C, I>,
    std::conditional_t<alternativeProvision<C, I> == Provision::inaccessible, InaccessibleAlternative<C, I>,
                       std::conditional_t<providesBase<C, I> && alternativeBase<C, I>, ProvidesAndAlternative<C, I>,
                                          NotDerivedFromInterface<C, I>>>>;

template <class T, class C>
inline constexpr bool isAlternative = alternativeBase<C, T> || contains<T, ListedAlternativesOf<C>>;

// Provides<T>/Alternative<T> listed by mistake, or a C that can't be used as a T, count too,
// so that ProvidesAsDependency or NotDerivedFromInterface is the one error.
template <class T, class C>
inline constexpr bool isProvider =
    std::is_same_v<T, C> || providesBase<C, T> || alternativeBase<C, T> || contains<T, ListedOf<C>>;

// With the root's getComponent<T>() and run(&T::f), the types keel asks the components about.
template <class Entry>
inline constexpr bool isDependencyEntry =
    (Split<Entry>::use == Use::required || Split<Entry>::use == Use::optional ||
     Split<Entry>::use == Use::ordering || Split<Entry>::use == Use::all || Split<Entry>::use == Use::optionalAll) &&
    couldBeComponent<typename Split<Entry>::target> && !isBundle<typename Split<Entry>::target>;
template <class Entry>
inline constexpr bool isReferral = isDependencyEntry<Entry>;

template <class T, class Entry>
inline constexpr bool isDependencyOn = isReferral<Entry> && std::is_same_v<typename Split<Entry>::target, T>;
template <class T, class Entries>
inline constexpr bool hasDependencyOn = false;
template <class T, class... Entries>
inline constexpr bool hasDependencyOn<T, TypeList<Entries...>> = (isDependencyOn<T, Entries> || ...);
// A list with a duplicate isn't checked further.
template <class T, class C>
inline constexpr bool dependsOn = uniqueEntriesOf<C> && hasDependencyOn<T, EntriesOf<C>>;

template <class T, class... Entries>
struct FirstDependencyOn {
    using type = void;
};
template <class T, class Entry, class... Entries>
struct FirstDependencyOn<T, Entry, Entries...>
    : std::conditional_t<isDependencyOn<T, Entry>, std::type_identity<Entry>, FirstDependencyOn<T, Entries...>> {};
template <class T, class List>
struct FirstDependencyIn;
template <class T, class... Entries>
struct FirstDependencyIn<T, TypeList<Entries...>> : FirstDependencyOn<T, Entries...> {};

template <class List>
struct ProviderFailuresOf;
template <class... Failures>
struct ProviderFailuresOf<TypeList<Failures...>> {
    using type = ProviderFailures<Failures...>;
};

template <class List>
struct Composition;

template <class... Cs>
struct Composition<TypeList<Cs...>> {
    static constexpr std::size_t size = sizeof...(Cs);
    using Row = std::array<bool, size>;

    template <std::size_t I>
    using At = std::tuple_element_t<I, std::tuple<Cs...>>;

    static constexpr std::size_t count(const Row& row) {
        std::size_t n = 0;
        for (bool b : row) n += b ? 1 : 0;
        return n;
    }

    template <class T>
    static constexpr Row providers{isProvider<T, Cs>...};
    template <class T>
    static constexpr std::size_t providerCount = count(providers<T>);

    template <class T, std::size_t... Is>
    static auto providersOf(std::index_sequence<Is...>)
        -> Concat<std::conditional_t<providers<T>[Is], TypeList<At<Is>>, TypeList<>>...>;
    template <class T>
    using Providers = decltype(providersOf<T>(std::make_index_sequence<size>{}));

    // Several providers are fine for one T if each is a keel::Alternative<T>.
    template <class T>
    static constexpr Row alternatives{isAlternative<T, Cs>...};
    template <class T>
    static constexpr std::size_t alternativeCount = count(alternatives<T>);
    template <class T>
    static constexpr bool chosenAtRuntime = providerCount<T> > 0 && alternativeCount<T> == providerCount<T>;
    // Reported once per T as MixedProviders, not once per place that needs one T.
    template <class T>
    static constexpr bool mixedProviders = alternativeCount<T> > 0 && alternativeCount<T> < providerCount<T>;
    template <class T, std::size_t... Is>
    static auto mixedOf(std::index_sequence<Is...>)
        -> MixedProviders<T,
                          Concat<std::conditional_t<providers<T>[Is] && !alternatives<T>[Is], TypeList<At<Is>>,
                                                    TypeList<>>...>,
                          Concat<std::conditional_t<alternatives<T>[Is], TypeList<At<Is>>, TypeList<>>...>>;
    template <class T>
    using Mixed = decltype(mixedOf<T>(std::make_index_sequence<size>{}));

    // What a lookup of T finds, in init order.
    template <class T>
    static constexpr std::array<std::size_t, providerCount<T>> providersInOrderOf() {
        std::array<std::size_t, providerCount<T>> out{};
        std::size_t n = 0;
        for (std::size_t k = 0; k < size && n < out.size(); ++k) {
            if (providers<T>[order.index[k]]) out[n++] = order.index[k];
        }
        return out;
    }
    template <class T>
    static constexpr std::array<std::size_t, providerCount<T>> providersInOrder = providersInOrderOf<T>();

    // Each broken provider is reported once, at the first dependency on T (else at the root);
    // the other dependencies on T pass, since their errors would follow from it.
    template <class T>
    static constexpr Row broken{brokenProvider<Cs, T>...};
    template <class T>
    static constexpr std::size_t brokenCount = count(broken<T>);
    template <class T, std::size_t... Is>
    static auto brokenOf(std::index_sequence<Is...>)
        -> Concat<std::conditional_t<broken<T>[Is], TypeList<ProviderFailure<At<Is>, T>>, TypeList<>>...>;
    template <class T>
    using Broken = typename ProviderFailuresOf<decltype(brokenOf<T>(std::make_index_sequence<size>{}))>::type;

    template <class T>
    static constexpr Row dependents{dependsOn<T, Cs>...};
    template <class T>
    static constexpr std::size_t dependentCount = count(dependents<T>);
    static constexpr std::size_t first(const Row& row) {
        std::size_t i = 0;
        while (i < size && !row[i]) ++i;
        return i;
    }
    // Entry of C is where the broken providers of T are reported.
    template <class C, class Entry, class T>
    static constexpr bool reportsBroken() {
        constexpr std::size_t i = first(dependents<T>);
        if constexpr (i < size) {
            return std::is_same_v<C, At<i>> && std::is_same_v<Entry, typename FirstDependencyIn<T, EntriesOf<C>>::type>;
        } else {
            return false;
        }
    }
    template <class T>
    using RootBroken = std::conditional_t<(brokenCount<T> == 0 || dependentCount<T> > 0), Passed<T>, Broken<T>>;

    template <class C, class T>
    static constexpr Row others{(!std::is_same_v<C, Cs> && isProvider<T, Cs>)...};
    template <class C, class T>
    static constexpr Row unconditionalOthers{(!std::is_same_v<C, Cs> && isProvider<T, Cs> && !isConditional<Cs>)...};
    template <class C, class T, std::size_t... Is>
    static auto othersOf(std::index_sequence<Is...>)
        -> Concat<std::conditional_t<others<C, T>[Is], TypeList<At<Is>>, TypeList<>>...>;
    template <class C, class T>
    using Others = decltype(othersOf<C, T>(std::make_index_sequence<size>{}));

    // For a dependency of C on T: the likely forgotten keel::Provides<T>.
    template <class C, class T>
    static constexpr Row derived{(!std::is_same_v<C, Cs> && !isProvider<T, Cs> && implements<Cs, T>())...};
    template <class C, class T, std::size_t... Is>
    static auto derivedOf(std::index_sequence<Is...>)
        -> Concat<std::conditional_t<derived<C, T>[Is], TypeList<At<Is>>, TypeList<>>...>;
    template <class C, class T>
    using Derived = decltype(derivedOf<C, T>(std::make_index_sequence<size>{}));

    // Passed<...> if fine, else the named failure (diagnostics.hpp), wrapped in an Outcome<>.
    template <class C, class Entry>
    static auto check() {
        using T = typename Split<Entry>::target;
        constexpr Use use = Split<Entry>::use;
        constexpr bool collection = use == Use::all || use == Use::optionalAll;
        if constexpr (use == Use::listedAsync) {
            return Outcome<AsyncAsDependency<C, T>>{};
        } else if constexpr (use == Use::listedProvides) {
            return Outcome<ProvidesAsDependency<C, T>>{};
        } else if constexpr (use == Use::listedAlternative) {
            return Outcome<AlternativeAsDependency<C, T>>{};
        } else if constexpr (isAsyncMarker<T>) {
            return Outcome<MisplacedAsync<C, Entry>>{};
        } else if constexpr (isBundle<T>) {
            return Outcome<BundleAsDependency<C, T>>{};
        } else if constexpr (use == Use::misnested) {
            return Outcome<MisnestedAll<C, T>>{};
        } else if constexpr (!couldBeComponent<T>) {
            return Outcome<InvalidDependency<C, Entry>>{};
        } else if constexpr (brokenCount<T> > 0) {
            if constexpr (reportsBroken<C, Entry, T>()) {
                return Outcome<Broken<T>>{};
            } else {
                return Outcome<Passed<C, Entry>>{};
            }
        } else if constexpr (providerCount<T> == 0 || collection) {
            // For All<T>, a component that derives from T without providing it is the likely mistake.
            using Subclass = FirstOr<Derived<C, T>, void>;
            if constexpr (!std::is_void_v<Subclass>) {
                if constexpr (collection) {
                    return Outcome<LeftOutOfAll<C, T, Subclass>>{};
                } else if constexpr (derivesFromComponent<T>) {
                    return Outcome<UndeclaredSubclass<C, T, Subclass>>{};
                } else {
                    return Outcome<UndeclaredProvider<C, T, Subclass>>{};
                }
            } else if constexpr (use == Use::required) {
                return Outcome<MissingDependency<C, T>>{};
            } else if constexpr (use == Use::all && count(others<C, T>) == 0) {
                return Outcome<MissingProvider<C, T>>{};
            } else if constexpr (use == Use::all && count(unconditionalOthers<C, T>) == 0) {
                return Outcome<OnlyConditionalProviders<C, T, Others<C, T>>>{};
            } else {
                return Outcome<Passed<C, Entry>>{};  // absent and optional or ordering only, or a collection
            }
        } else if constexpr (providerCount<T> > 1 && use != Use::ordering && mixedProviders<T>) {
            return Outcome<Mixed<T>>{};
        } else if constexpr (providerCount<T> > 1 && use != Use::ordering && !chosenAtRuntime<T>) {
            return Outcome<AmbiguousDependency<C, T, Providers<T>>>{};
        } else if constexpr (use == Use::required && !chosenAtRuntime<T> &&
                             isConditional<FirstOr<Providers<T>, void>>) {
            using Provider = FirstOr<Providers<T>, void>;
            if constexpr (std::is_same_v<Provider, T>) {
                return Outcome<RequiredConditional<C, T>>{};
            } else {
                return Outcome<RequiredConditionalProvider<C, T, Provider>>{};
            }
        } else {
            return Outcome<Passed<C, Entry>>{};  // After<T> orders after every provider
        }
    }
    template <class C, class Entry>
    using Check = typename decltype(check<C, Entry>())::type;

    // For several non-alternative providers: none, so it is reported as ambiguous, not as a cycle too.
    template <class C, class Entry>
    static constexpr Row edgesOf() {
        using T = typename Split<Entry>::target;
        constexpr Use use = Split<Entry>::use;
        Row row{};
        if constexpr (use == Use::ordering || use == Use::all || use == Use::optionalAll) {
            row = others<C, T>;
        } else if constexpr ((use == Use::required || use == Use::optional) &&
                             (providerCount<T> <= 1 || chosenAtRuntime<T>)) {
            row = providers<T>;
        }
        return row;
    }
    static constexpr void addTo(Row& row, const Row& edges) {
        for (std::size_t i = 0; i < size; ++i) row[i] = row[i] || edges[i];
    }
    template <class C, class... Entries>
    static constexpr Row rowOf(TypeList<Entries...>) {
        Row row{};
        (addTo(row, edgesOf<C, Entries>()), ...);
        return row;
    }
    // One constant per component, so the compiler's constexpr step limit applies per row.
    template <class C>
    static constexpr Row row = rowOf<C>(EntriesOf<C>{});

    static constexpr Graph<size> graph{row<Cs>...};
    static constexpr TopologicalOrder<size> order = topologicalOrder(graph);
    static constexpr bool acyclic = order.count == size;
    static constexpr Row onCycle = acyclic ? Row{} : componentsOnCycles(graph, order.placed);

    using Cycle = typename KeepIf<TypeList<Cs...>, onCycle, std::make_index_sequence<size>>::type;
    using Sorted = std::conditional_t<
        acyclic, typename Reorder<TypeList<Cs...>, order.index, std::make_index_sequence<size>>::type, TypeList<Cs...>>;
};

template <class T, bool component = derivesFromComponent<T>>
inline constexpr bool abstractComponent = false;
template <class T>
inline constexpr bool abstractComponent<T, true> = std::is_abstract_v<T>;

// Only read for a component (a complete class).
template <class T, bool component = derivesFromComponent<T>>
inline constexpr bool inaccessibleAsync = false;
template <class T>
inline constexpr bool inaccessibleAsync<T, true> = (std::is_base_of_v<AsyncInit, T> && !hasAsyncInit<T>) ||
                                                   (std::is_base_of_v<AsyncStart, T> && !hasAsyncStart<T>);

// Only read for a component (a complete class).
template <class T, bool component = derivesFromComponent<T>>
inline constexpr bool inaccessibleConditional = false;
template <class T>
inline constexpr bool inaccessibleConditional<T, true> = std::is_base_of_v<Conditional, T> && !isConditional<T>;

template <class... Cs>
struct ListInfo {
    static constexpr std::size_t size = sizeof...(Cs);
    static constexpr std::array<TypeId, size> ids{typeId<Cs>()...};

    // Compares types, not ids: GCC 11 doesn't compare addresses of different objects at compile time.
    template <std::size_t I>
    static constexpr std::size_t earlier = [] {
        using T = std::tuple_element_t<I, std::tuple<Cs...>>;
        constexpr bool same[] = {std::is_same_v<Cs, T>...};
        std::size_t n = 0;
        for (std::size_t j = 0; j < I; ++j) n += same[j] ? 1 : 0;
        return n;
    }();

    // A type listed twice is reported once, at its second entry.
    template <class C, std::size_t I>
    using Check = std::conditional_t<
        earlier<I> == 1, DuplicateComponent<C>,
        std::conditional_t<
            earlier<I> == 0 && !derivesFromComponent<C>, NotAComponent<C>,
            std::conditional_t<
                earlier<I> == 0 && inaccessibleConditional<C>, InaccessibleConditional<C>,
                std::conditional_t<earlier<I> == 0 && inaccessibleAsync<C>, InaccessibleAsync<C>,
                                   std::conditional_t<earlier<I> == 0 && abstractComponent<C>, AbstractComponent<C>,
                                                      Passed<C, std::integral_constant<std::size_t, I>>>>>>>;
};

// Each check is an outcome type: compose<...> derives from them (a failure's static_assert
// fires), isValid<...> tests them (nothing is instantiated).

template <class List, class Indices>
struct ListDiagnosis;
template <class... Cs, std::size_t... Is>
struct ListDiagnosis<ListInfo<Cs...>, std::index_sequence<Is...>> : ListInfo<Cs...>::template Check<Cs, Is>... {};
template <class List, class Indices>
inline constexpr bool listPasses = false;
template <class... Cs, std::size_t... Is>
inline constexpr bool listPasses<ListInfo<Cs...>, std::index_sequence<Is...>> =
    (isPassed<typename ListInfo<Cs...>::template Check<Cs, Is>> && ...);

template <class Info, class C, class Entries = EntriesOf<C>>
struct ComponentDiagnosis;
// Checked per component, whatever refers to its interfaces. The tag is a type no entry can name,
// so it never repeats an entry's Passed<C, Entry> base.
struct AlternativeConditionalChecked {};
template <class C>
using ConditionalCheck = std::conditional_t<std::is_base_of_v<AlternativeMarker, C> && !isConditional<C>,
                                            AlternativeNotConditional<C>, Passed<C, AlternativeConditionalChecked>>;
template <class Info, class C, class... Entries>
struct ComponentDiagnosis<Info, C, TypeList<Entries...>> : ConditionalCheck<C>, Info::template Check<C, Entries>... {};
template <class Info, class C, class Entries = EntriesOf<C>>
inline constexpr bool entriesPass = false;
template <class Info, class C, class... Entries>
inline constexpr bool entriesPass<Info, C, TypeList<Entries...>> =
    isPassed<ConditionalCheck<C>> && (isPassed<typename Info::template Check<C, Entries>> && ...);

// A list with a duplicate is not checked further: the duplicate is the one error.
template <class Info, class C>
using ComponentOutcome = std::conditional_t<uniqueEntriesOf<C>, ComponentDiagnosis<Info, C>,
                                            ComponentListCheck<ComponentOf<C>>>;
template <class Info, class C>
inline constexpr bool componentPasses = uniqueEntriesOf<C> && entriesPass<Info, C>;

// Only on a well-formed list (naming the checks reads each component's list); otherwise the
// list's own check is the error.
template <class Info, class List>
struct ComponentsDiagnosis;
template <class Info, class... Cs>
struct ComponentsDiagnosis<Info, TypeList<Cs...>> : ComponentOutcome<Info, Cs>... {};
template <bool wellFormed, class Info, class List>
inline constexpr bool componentsPass = true;
template <class Info, class... Cs>
inline constexpr bool componentsPass<true, Info, TypeList<Cs...>> = (componentPasses<Info, Cs> && ...);

template <class Repeated>
struct BundleDiagnosis;
template <class... Bundles>
struct BundleDiagnosis<TypeList<Bundles...>> : DuplicateBundle<Bundles>... {};

// The graph analysis runs only on a list of distinct, concrete components, so a list that
// isn't one reports that and nothing else.
template <class Expansion>
struct Analysis;
template <class... Cs, class Repeated>
struct Analysis<Expanded<TypeList<Cs...>, Repeated>> {
    using List = ListInfo<Cs...>;
    using Indices = std::index_sequence_for<Cs...>;
    static constexpr bool wellFormed = listPasses<List, Indices>;
    using Info = Composition<std::conditional_t<wellFormed, TypeList<Cs...>, TypeList<>>>;

    using OfBundles = BundleDiagnosis<Repeated>;
    using OfList = ListDiagnosis<List, Indices>;
    using OfComponents = std::conditional_t<wellFormed, ComponentsDiagnosis<Info, TypeList<Cs...>>, Passed<>>;
    using NonEmpty = std::conditional_t<(sizeof...(Cs) > 0), Passed<>, EmptyComposition<>>;
    using Acyclic = std::conditional_t<Info::acyclic, Passed<>, DependencyCycle<typename Info::Cycle>>;

    // What keel::isValid<...> returns.
    static constexpr bool ok = std::is_same_v<Repeated, TypeList<>> && wellFormed &&
                               componentsPass<wellFormed, Info, TypeList<Cs...>> && isPassed<NonEmpty> &&
                               isPassed<Acyclic>;
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

// One object for every Slot: GCC 11 rejects expanding args inside the expansion over the slots.
template <class... Args>
struct Arguments {
    std::tuple<Args&...> refs;
};

template <class C>
struct Slot {
    template <class... Args>
    explicit Slot(const Arguments<Args...>& arguments)
        : component(std::apply([](Args&... args) { return construct<C>(args...); }, arguments.refs)) {}
    C component;
};

template <class List>
struct Storage;
template <class... Sorted>
struct Storage<TypeList<Sorted...>> : Slot<Sorted>... {
    template <class... Args>
    explicit Storage(Args&... args) : Storage(Arguments<Args...>{{args...}}) {}
    template <class... Args>
    explicit Storage(const Arguments<Args...>& arguments) : Slot<Sorted>(arguments)... {}

    template <class T>
    T& get() noexcept {
        return static_cast<Slot<T>&>(*this).component;
    }
};

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

// Defined below; detail::isApp and detail::runApp name it.
template <class... Components>
class App;

namespace detail {

template <class Expansion>
class AppImpl;

// Escapes s for a quoted DOT string (DOT reads \n, \l, \" as escapes).
inline void appendDotEscaped(std::string& out, std::string_view s) {
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
}

// The arrows of one entry of a component's list. Not a template, so a large app's toDot()
// compiles one loop, not one per entry.
inline void appendDotEdges(std::string& out, std::size_t from, std::span<const bool> targets,
                           std::span<const TypeId> ids, TypeId target, std::string_view style,
                           std::string_view targetName) {
    for (std::size_t j = 0; j < targets.size(); ++j) {
        if (!targets[j]) continue;
        out += concat({"  n", std::to_string(from), " -> n", std::to_string(j)});
        std::string attributes(style);
        if (ids[j] != target) {
            attributes += attributes.empty() ? "label=\"" : ", label=\"";
            appendDotEscaped(attributes, targetName);
            attributes += '"';
        }
        out += attributes.empty() ? ";\n" : concat({" [", attributes, "];\n"});
    }
}

// Each failed check is one error.
template <class... Cs, class Repeated>
class AppImpl<Expanded<TypeList<Cs...>, Repeated>> {
    using Checks = Analysis<Expanded<TypeList<Cs...>, Repeated>>;
    static constexpr bool wellFormed = Checks::wellFormed;
    using Info = typename Checks::Info;

    static_assert(sizeof(typename Checks::OfBundles) > 0);     // names a bundle reached twice
    static_assert(sizeof(typename Checks::OfList) > 0);        // names a non-component or a duplicate
    static_assert(sizeof(typename Checks::OfComponents) > 0);  // names missing dependencies, ...
    static_assert(sizeof(typename Checks::NonEmpty) > 0);
    static_assert(sizeof(typename Checks::Acyclic) > 0);

public:
    template <class... Args>
    explicit AppImpl(Args&&... args) : storage_(args...) {
        addAll(typename Info::Sorted{});  // in dependency order
        forEachReferred([&]<class T>() { bindProviders<T>(std::make_index_sequence<Info::template providerCount<T>>{}); });
        if constexpr (anyAsync(typename Info::Sorted{})) registry_.setOrdering(&ordered);
    }

    // Without the deleted move, the constructor template would be picked for a move.
    AppImpl(const AppImpl&) = delete;
    AppImpl(AppImpl&&) = delete;
    AppImpl& operator=(const AppImpl&) = delete;
    AppImpl& operator=(AppImpl&&) = delete;

    ~AppImpl() { registry_.deinit(); }

    /// Calls init() on every component in dependency order; rolls back and rethrows on failure.
    void init() { registry_.init(); }
    /// false runs every component on the calling thread; call before init().
    void setAsync(bool async) { registry_.setAsync(async); }
    /// Calls start() on every enabled component in dependency order; rolls back on failure.
    void start() { registry_.start(); }
    /// Calls stop() on every started component, in reverse order.
    void stop() noexcept { registry_.stop(); }
    /// Stops if started, then calls deinit() on every component in reverse order; idempotent.
    void deinit() noexcept { registry_.deinit(); }

    /// A component, or the one provider of an interface, for the composition root.
    template <class T>
    T& getComponent() {
        constexpr std::size_t providers = Info::template providerCount<T>;
        constexpr bool chosenAtRuntime = Info::template chosenAtRuntime<T>;
        using Providers = typename Info::template Providers<T>;
        using Provider = FirstOr<Providers, void>;
        // One mistake, one error: an invalid compose<...> list is reported there, not here.
        using Diagnosis = std::conditional_t<
            !wellFormed, Passed<T>,
            std::conditional_t<
                (Info::template brokenCount<T> > 0), typename Info::template RootBroken<T>,
                std::conditional_t<
                    providers == 1 || chosenAtRuntime, Passed<T>,
                    std::conditional_t<providers == 0, NotInApp<T>,
                                       std::conditional_t<Info::template mixedProviders<T>,
                                                          typename Info::template Mixed<T>,
                                                          AmbiguousInterface<T, Providers>>>>>>;
        static_assert(sizeof(Diagnosis) > 0);
        if constexpr ((providers == 1 || chosenAtRuntime) && implements<Provider, T>()) {  // else an error above
            // Built here (T may have no bindings), on the stack so the lookup doesn't allocate.
            // One provider resolves before init() too, even a keel::Conditional: the root may configure it.
            const auto bindings = bindingsOf<T>(std::make_index_sequence<providers>{});
            return *static_cast<T*>(registry_.resolve(bindings, typeName<T>(), {}, true)->object);
        } else {
            throw Error("keel: unreachable");
        }
    }

    /// Component names in init order, known at compile time.
    static constexpr std::array<std::string_view, Info::size> initOrder() noexcept {
        return namesOf(typename Info::Sorted{});
    }

    /// How long each lifecycle call took, in the order the calls finished.
    std::span<const Timing> timings() const noexcept { return registry_.timings(); }

    /// The dependency graph in Graphviz DOT format.
    std::string toDot() const {
        std::string out = "digraph keel {\n  rankdir=LR;\n  node [shape=box];\n";
        std::vector<std::string> interfaces(Info::size);  // per node: "\nprovides I", ...
        forEachReferred([&interfaces]<class T>() {
            dotInterfaces<T>(interfaces, std::make_index_sequence<Info::template providerCount<T>>{});
        });
        dotNodes(out, interfaces, std::make_index_sequence<Info::size>{});
        dotEdges(out, std::make_index_sequence<Info::size>{});
        return out + "}\n";
    }

protected:
    template <class Self, class Fn>
    static decltype(auto) runWith(Self& self, Fn&& fn) {
        Registry& registry = static_cast<AppImpl&>(self).registry_;
        registry.init();
        struct Shutdown {
            Registry& registry;
            ~Shutdown() { registry.deinit(); }  // stops first
        } shutdown{registry};
        return call(self, registry, std::forward<Fn>(fn));
    }

    // One mistake gives exactly one error: a run diagnostic, or the compiler's own inside a
    // generic callable. A member function's target is resolved before start().
    template <class Self, class Fn>
    static decltype(auto) call([[maybe_unused]] Self& self, Registry& registry, Fn&& fn) {
        using F = std::remove_cvref_t<Fn>;
        if constexpr (std::is_member_function_pointer_v<F>) {
            using C = typename MemberOf<F>::type;  // a component, or an interface (Provides<C>)
            constexpr std::size_t targets = wellFormed ? Info::template providerCount<C> : 1;
            if constexpr (Info::template brokenCount<C> > 0) {
                // A provider of C that can't be used as one, reported here or by compose<...>.
                using Broken = typename Info::template RootBroken<C>;
                if constexpr (isPassed<Broken>) {
                    return RunMistake{};
                } else {
                    return RunTargetMistake<Broken>{};
                }
            } else if constexpr (targets == 0) {
                return RunTargetNotComposed<C, F>{};
            } else if constexpr (targets > 1 && Info::template mixedProviders<C>) {
                return RunTargetMistake<typename Info::template Mixed<C>>{};
            } else if constexpr (targets > 1 && !Info::template chosenAtRuntime<C>) {
                return RunTargetAmbiguous<C, typename Info::template Providers<C>>{};
            } else if constexpr (!Info::template chosenAtRuntime<C> &&
                                 isConditional<FirstOr<typename Info::template Providers<C>, void>>) {
                return RunTargetConditional<C, FirstOr<typename Info::template Providers<C>, void>>{};
            } else if constexpr (!std::is_invocable_v<F, C&>) {
                return RunTargetNotCallable<F>{};
            } else {
                C& target = self.template getComponent<C>();
                registry.start();
                return (target.*fn)();
            }
        } else if constexpr (std::is_invocable_v<Fn>) {
            registry.start();
            if constexpr (std::is_invocable_v<Fn, Self&>) {
                return std::forward<Fn>(fn)(self);
            } else {
                return std::forward<Fn>(fn)();
            }
        } else if constexpr (std::is_class_v<F> && !hasPlainCallOperator<F>) {
            // A generic lambda is called with the App directly, so an error in its body is reported
            // once by the compiler instead of turning into "not callable".
            registry.start();
            return std::forward<Fn>(fn)(self);
        } else if constexpr (std::is_invocable_v<Fn, Self&>) {
            registry.start();
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

    // Calls visit.template operator()<T>() once per type a component depends on.
    template <class Visit>
    static void forEachReferred(Visit&& visit) {
        std::vector<TypeId> seen;
        referredBy(visit, seen, typename Info::Sorted{});
    }
    template <class Visit, class... Sorted>
    static void referredBy(Visit& visit, std::vector<TypeId>& seen, TypeList<Sorted...>) {
        (referredIn(visit, seen, EntriesOf<Sorted>{}), ...);
    }
    template <class Visit, class... Entries>
    static void referredIn(Visit& visit, std::vector<TypeId>& seen, TypeList<Entries...>) {
        (referredAt<typename Split<Entries>::target, isReferral<Entries>>(visit, seen), ...);
    }
    // One instantiation per type, not per entry (T, Optional<T>, ... name the same T).
    template <class T, bool referral, class Visit>
    static void referredAt([[maybe_unused]] Visit& visit, [[maybe_unused]] std::vector<TypeId>& seen) {
        if constexpr (referral) {
            for (TypeId id : seen) {
                if (id == typeId<T>()) return;
            }
            seen.push_back(typeId<T>());
            visit.template operator()<T>();
        }
    }

    template <class T, std::size_t... Js>
    std::array<Binding, sizeof...(Js)> bindingsOf(std::index_sequence<Js...>) {
        return {bindingOf<T, typename Info::template At<Info::template providersInOrder<T>[Js]>>()...};
    }
    template <class T, class C>
    Binding bindingOf() {
        C& component = storage_.template get<C>();
        void* object = nullptr;
        if constexpr (implements<C, T>()) object = static_cast<void*>(static_cast<T*>(&component));
        return Binding{&static_cast<ComponentBase&>(component), object, isAlternative<T, C>};
    }

    // A provider that can't be used as a T is a compile error, and not bound.
    template <class T, std::size_t... Js>
    void bindProviders(std::index_sequence<Js...>) {
        (bindProvider<T, typename Info::template At<Info::template providersInOrder<T>[Js]>>(), ...);
    }
    template <class T, class C>
    void bindProvider() {
        if constexpr (implements<C, T>()) {
            registry_.bind(typeId<T>(), bindingOf<T, C>());
        }
    }

    template <class... Sorted>
    static constexpr bool anyAsync(TypeList<Sorted...>) noexcept {
        return ((hasAsyncInit<Sorted> || hasAsyncStart<Sorted>) || ...);
    }
    // The ordering edges by init position: what the async scheduler waits for.
    static bool ordered(std::size_t k, std::size_t m) noexcept {
        return Info::graph[Info::order.index[k]][Info::order.index[m]];
    }

    template <class... Sorted>
    static constexpr std::array<std::string_view, sizeof...(Sorted)> namesOf(TypeList<Sorted...>) noexcept {
        return {typeName<Sorted>()...};
    }

    // Node n<i> is the i-th component of the compose<...> list; written in init order (Ks).
    template <std::size_t... Ks>
    void dotNodes(std::string& out, const std::vector<std::string>& interfaces, std::index_sequence<Ks...>) const {
        (dotNode<typename Info::template At<Info::order.index[Ks]>>(out, Info::order.index[Ks],
                                                                    interfaces[Info::order.index[Ks]],
                                                                    registry_.disabled(Ks)),
         ...);
    }
    template <class C>
    static void dotNode(std::string& out, std::size_t i, std::string_view interfaces, bool disabled) {
        out += concat({"  n", std::to_string(i), " [label=\""});
        appendDotEscaped(out, typeName<C>());
        out += interfaces;
        if constexpr (hasAsyncInit<C>) out += "\\nasync init";
        if constexpr (hasAsyncStart<C>) out += "\\nasync start";
        out += disabled ? "\\n(disabled)\", color=gray, fontcolor=gray];\n" : "\"];\n";
    }
    template <class T, std::size_t... Js>
    static void dotInterfaces([[maybe_unused]] std::vector<std::string>& interfaces, std::index_sequence<Js...>) {
        (dotInterface<T, Info::template providersInOrder<T>[Js]>(interfaces), ...);
    }
    template <class T, std::size_t i>
    static void dotInterface(std::vector<std::string>& interfaces) {
        using C = typename Info::template At<i>;
        // A component that provides itself shows nothing.
        if constexpr (!std::is_same_v<C, T> && implements<C, T>()) {
            interfaces[i] += isAlternative<T, C> ? "\\nalternative " : "\\nprovides ";
            appendDotEscaped(interfaces[i], typeName<T>());
        }
    }

    template <std::size_t... Ks>
    static void dotEdges(std::string& out, std::index_sequence<Ks...>) {
        (dotEdgesOf<typename Info::template At<Info::order.index[Ks]>>(
             out, Info::order.index[Ks], EntriesOf<typename Info::template At<Info::order.index[Ks]>>{}),
         ...);
    }
    template <class C, class... Entries>
    static void dotEdgesOf([[maybe_unused]] std::string& out, [[maybe_unused]] std::size_t from,
                           TypeList<Entries...>) {
        (dotEdgesOfEntry<C, Entries>(out, from), ...);
    }
    template <class C, class Entry>
    static void dotEdgesOfEntry(std::string& out, std::size_t from) {
        using T = typename Split<Entry>::target;
        static constexpr typename Info::Row targets = Info::template edgesOf<C, Entry>();
        appendDotEdges(out, from, targets, Checks::List::ids, typeId<T>(), dotStyle(Split<Entry>::use), typeName<T>());
    }
    static constexpr std::string_view dotStyle(Use use) noexcept {
        switch (use) {
            case Use::optional:
                return "style=dashed";
            case Use::ordering:
                return "style=dotted, arrowhead=empty";
            case Use::all:
                return "style=bold";
            case Use::optionalAll:
                return "style=\"bold,dashed\"";
            case Use::required:
            case Use::listedProvides:  // no edges
            case Use::listedAlternative:
            case Use::misnested:
            case Use::listedAsync:
                return "";
        }
        return "";
    }

    Registry registry_;  // declared first: outlives the components
    Storage<typename Info::Sorted> storage_;
};

template <class T>
inline constexpr bool isApp = false;
template <class... Cs>
inline constexpr bool isApp<App<Cs...>> = true;

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

/// A statically composed application.
template <class... Components>
class App : public detail::AppImpl<detail::ExpandBundles<Components...>> {
    using Base = detail::AppImpl<detail::ExpandBundles<Components...>>;

public:
    using Base::Base;

    /// Runs init(), start(), fn, stop() and deinit(); returns what fn returns.
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

/// Constructs a keel::App; components constructible from args receive them.
template <class... Components, class... Args>
App<Components...> compose(Args&&... args) {
    return App<Components...>(std::forward<Args>(args)...);
}

/// Whether compose<Components...> would pass the compile-time checks; never a compile error.
template <class... Components>
inline constexpr bool isValid = detail::Analysis<detail::ExpandBundles<Components...>>::ok;

/// true, or the compile error compose<Components...> would give.
template <class... Components>
inline constexpr bool requireValid = sizeof(App<Components...>) > 0;

/// Constructs AppType from args and runs it; returns fn's result by value.
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

}  // namespace keel
