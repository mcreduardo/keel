// keel.hpp: application-level components with declared dependencies.
//
// Single header, C++20, no RTTI.
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

/// Composition and usage errors: a dependency that isn't composed, a dependency used
/// before it is initialized, and so on.
class Error : public std::logic_error {
public:
    using std::logic_error::logic_error;
};

/// A lifecycle hook of a component, as recorded in keel::Timing.
enum class Phase { init, start, stop, deinit };

/// "init", "start", "stop" or "deinit".
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

/// How long one lifecycle call of one component took (App::timings()).
struct Timing {
    std::string_view component;            // the component's name, as in App::initOrder()
    Phase phase = Phase::init;
    std::chrono::nanoseconds duration{};   // measured with std::chrono::steady_clock
    bool failed = false;                   // the call threw (init() or start()); the rollback follows
};

/// Marks a dependency in keel::Component<...> as optional: the component works without
/// it. If T is in the app (composed, or an interface that one composed component derives
/// keel::Provides<> of), keel constructs and initializes it before this component (and
/// deinitializes and destroys it after), and findComponent<T>() returns it, or nullptr if
/// it is a keel::Conditional that is disabled; otherwise findComponent<T>() returns nullptr.
/// This is how to use a keel::Conditional component: a required dependency on one doesn't
/// compile (RequiredConditional).
/// Use findComponent<T>(), not getComponent<T>(). When T is in the app, the dependency
/// counts like a required one: a cycle through it, or two providers of T (unless each is a
/// keel::Alternative<T>), is a compile error. So is T absent while a composed component derives from it without
/// keel::Provides<T> (UndeclaredProvider).
template <class T>
struct Optional {};

/// Ordering only: if T is also in the app's keel::compose<...> list, this component is
/// constructed and initialized after T, and deinitialized and destroyed before it. For an
/// interface T (keel::Provides<T>), after every component that provides it, except this
/// one. It gives no access to T; list T (or keel::Optional<T>) instead to use it. If T isn't
/// in the app, nothing happens, unless a composed component derives from T without
/// keel::Provides<T>: that is a compile error (UndeclaredProvider).
template <class T>
struct After {};

/// Declares that a component implements the interface I, a plain class it also derives from
/// publicly. It is a base class, next to keel::Component<...>, which lists only the
/// component's dependencies:
///   class DiskStorage : public keel::Component<Config>, public keel::Provides<Storage>, public Storage
/// Other components can then list Storage (or keel::Optional<Storage>, or
/// keel::After<Storage>) and get the DiskStorage. A component that implements several
/// interfaces derives from one keel::Provides<> per interface. A dependency on I needs
/// exactly one provider of I in the app; with two, it is a compile error (unless each of
/// them is a keel::Alternative<I>, chosen at runtime). keel::After<I> orders after every
/// provider, and keel::All<I> uses every provider. Dependencies match by exact type: a
/// component that derives from I without deriving from keel::Provides<I> is not found as I
/// (and keel reports it).
///
/// Derive from it publicly, once: privately (the default for a class), protectedly or
/// twice is InaccessibleProvides. Listing it in keel::Component<...> is
/// ProvidesAsDependency. I must be a class type that a dependency could name: not const or
/// volatile, not a reference, not a keel marker; it may be incomplete (forward-declared).
/// Anything else is a compile error where the component is constructed (compose<...>).
///
/// C++20 can't list the base classes of a class, so keel asks each composed component about
/// the types something refers to: the dependencies of the composed components (I,
/// keel::Optional<I>, keel::After<I>, keel::All<I>, ...), and App::getComponent<I>() and
/// run(&I::f) at the root. A keel::Provides<I> that nothing refers to has no effect: it isn't
/// checked (NotDerivedFromInterface, InaccessibleProvides) and App::toDot() doesn't show it.
///
/// To substitute a subclass for a component (a fake in tests), the component provides
/// itself, and its subclasses inherit that:
///   class Clock : public keel::Component<>, public keel::Provides<Clock> { ... };
///   class FakeClock : public Clock { ... };
///   keel::compose<Service, FakeClock>();  // Service's dependency on Clock gets FakeClock
template <class I>
struct Provides;  // defined below, after the check of I

/// Like keel::Provides<I>, for one of several implementations of I that configuration
/// chooses between at runtime. A base class, like keel::Provides<I>; each alternative is
/// also a keel::Conditional, and one of them is enabled:
///   class RedisCache : public keel::Component<Config>, public keel::Alternative<Cache>, public Cache,
///                      public keel::Conditional {
///       bool enabled() const override { return getComponent<Config>().cache() == "redis"; }
///   };
///   class MemoryCache : public keel::Component<Config>, public keel::Alternative<Cache>, public Cache,
///                       public keel::Conditional {
///       bool enabled() const override { return getComponent<Config>().cache() == "memory"; }
///   };
/// Other components depend on plain Cache (or keel::Optional<Cache>) and get the enabled one.
/// They are initialized after every alternative, so the choice is made first. When such a
/// component is initialized, init() throws keel::Error if no alternative is enabled (a
/// keel::Optional<Cache> gets nullptr instead) or if several are: the only composition
/// failures keel leaves to runtime. keel::Optional<keel::All<Cache>> gets the enabled ones.
/// An alternative that doesn't derive from keel::Conditional is a compile error
/// (AlternativeNotConditional), and so is a component that derives from keel::Provides<Cache>
/// next to the alternatives (MixedProviders) as soon as a component needs one Cache.
///
/// Derive from it publicly, once (else InaccessibleAlternative), and not next to
/// keel::Provides<Cache> (ProvidesAndAlternative); listing it in keel::Component<...> is
/// AlternativeAsDependency. As for keel::Provides<I>, keel asks the components about an I
/// only when something refers to it (a dependency, App::getComponent<I>(), run(&I::f)): an
/// alternative of an interface nothing refers to isn't checked, except that it must be a
/// keel::Conditional. I must be a class type a dependency could name, as for
/// keel::Provides<I>.
///
/// When the implementations need the same dependencies, a plain component that creates the
/// configured one in init() is simpler (docs/runtime-decisions.md).
template <class I>
struct Alternative;  // defined below, after the check of I

/// Every component that provides the interface I (derives from keel::Provides<I> or
/// keel::Alternative<I>, or is I itself), for plug-in style lists: health checks, routes,
/// sensors. Use getAll<I>(), which returns the enabled ones in init order (each after the
/// ones it depends on). At least one must be composed, else it is a compile error
/// (MissingProvider), and at least one of them must not be a keel::Conditional, so the list
/// is never empty (OnlyConditionalProviders); write keel::Optional<keel::All<I>> if none is
/// fine. Each provider is constructed and initialized before this component, and
/// deinitialized and destroyed after it. A component that provides I itself is not part of
/// its own list, nor is one that lists keel::All<I> and derives from I without providing it:
///   class HealthEndpoint : public keel::Component<keel::All<HealthCheck>> { ... };
///   class StatusPage : public keel::Component<keel::All<HealthCheck>>, public keel::Provides<HealthCheck>,
///                      public HealthCheck { ... };  // gets every HealthCheck but itself
///   class Plugins : public keel::Component<keel::Optional<keel::All<Plugin>>> { ... };
/// keel::All<keel::Optional<I>> is a compile error (MisnestedAll): write
/// keel::Optional<keel::All<I>>.
template <class I>
struct All {};

/// A base class that runs this component's init() (and enabled(), for a keel::Conditional)
/// on a thread of its own, so that a slow init() (connecting to a database, loading a
/// model) doesn't hold up unrelated components:
///   class Database : public keel::Component<Config, Logger>, public keel::AsyncInit { ... };
/// Its init() starts as soon as every component it is ordered after has finished init() (or
/// is disabled). Everything those init()s did is visible to it.
///
/// Meanwhile, the thread that called App::init() initializes the other components in init
/// order. When it next picks one, it prefers those that an async component waits for, and
/// it skips the ones still waiting for an async component. So components with no
/// dependency path between them may run in a different order than without async.
///
/// This affects every component of the app, not only the async ones: two components with
/// no dependency path between them may now run at the same time, sync ones included. A
/// dependency they share (a Logger, a Metrics registry) must then be thread-safe, and so
/// must any other state they share.
///
/// Thread affinity: init() runs on a worker thread, while stop() and deinit() run on the
/// thread that drives the App. Don't rely on thread_local state, thread-bound contexts
/// (OpenGL, CUDA, some SDKs) or locks held from init() to deinit().
///
/// Each async component gets its thread when the phase begins, even if it then waits for
/// its dependencies: one thread per async component. App::init() returns only when every
/// thread has finished.
///
/// If an init() (or an enabled()) throws, nothing new starts, and the init()s already running
/// finish. Everything initialized is then deinitialized in reverse order, on the calling
/// thread. The exception of the earliest failed component in init order is rethrown; the
/// others are dropped, and which components get to fail depends on timing. Each failure is
/// in App::timings() with `failed`. A thread that can't be created counts as a failure of
/// its component (std::system_error). App::setAsync(false) runs everything on the calling
/// thread instead, e.g. to debug.
///
/// Derive from it publicly, once (a subclass of an async component is async too):
/// privately, protectedly or twice is InaccessibleAsync. It is not a dependency: listing it
/// in keel::Component<...> is AsyncAsDependency.
struct AsyncInit {};

/// Like keel::AsyncInit, for start(): a base class that runs this component's start() on a
/// thread of its own, as soon as every component it is ordered after has finished start().
/// start() still begins only after every component's init() has finished, async ones
/// included.
///
/// start() must still return: its thread ends when it does, and App::start() waits for it.
/// For work that runs until stop(), start your own thread in start() and join it in stop().
/// Joining there also makes your thread's lookups (getComponent<T>()) safe: stop() runs
/// before any of your dependencies is stopped or deinitialized.
///
/// Thread affinity: start() runs on a worker thread, stop() on the thread that drives the
/// App. The rest is as for keel::AsyncInit (shared state, failures). If a start() throws,
/// the start()s already running finish, the started components are stopped, and every
/// component is deinitialized, in reverse order on the calling thread. Derive from it as
/// from keel::AsyncInit.
struct AsyncStart {};

/// A named group of components, to list in keel::compose<...> or keel::App<...> in place of
/// its components:
///   using Core = keel::Bundle<Config, Logger>;
///   using Http = keel::Bundle<Router, HttpServer>;
///   auto app = keel::compose<Core, Http, Exporter>();
/// is the same as keel::compose<Config, Logger, Router, HttpServer, Exporter>(): a bundle is
/// expanded in place, and may contain other bundles. As in any compose<...> list, the init
/// order comes from the dependencies, and components that don't depend on each other keep
/// the written order. Each component and each bundle is listed once: a component that two
/// bundles both contain is DuplicateComponent, a bundle reached twice is DuplicateBundle.
/// A bundle is not a dependency: keel::Component<Core> is an error (BundleAsDependency).
/// keel::App<Core, Router> and keel::App<Config, Logger, Router> have the same components
/// but are different types; a run() lambda that takes the App names the one you created.
template <class... Components>
struct Bundle {};

template <class... Dependencies>
class Component;

template <class... Components>
class App;

namespace detail {
class Registry;
}

/// A component that configuration may turn off. Derive from it publicly, next to
/// keel::Component<...>, and override enabled():
///   class Uplink : public keel::Component<Config, Recorder>, public keel::Conditional {
///       bool enabled() const override { return !getComponent<Config>().uplinkUrl().empty(); }
///       void start() override;  // only called if enabled
///   };
/// keel asks enabled() once, just before the component's init(), on the same thread (its
/// own thread with keel::AsyncInit). Its dependencies are initialized by then, so it can use
/// them (typically a Config). If it returns false, the component is disabled: none of its
/// hooks run (it is still constructed and destroyed). Throw to abort startup, as from
/// init().
///
/// Nothing may require a conditional component, since it may not be there, except through
/// keel::Alternative<I>, whose choice is checked at startup. A dependency on a conditional
/// T is keel::Optional<T> (findComponent<T>() returns nullptr while it is disabled),
/// keel::After<T> or keel::Optional<keel::All<I>>. A plain T, or an interface I whose
/// one provider is a conditional component, doesn't compile
/// (RequiredConditional, RequiredConditionalProvider), and neither does a keel::All<I>
/// whose providers are all conditional (OnlyConditionalProviders), nor run(&T::f) on one
/// (RunTargetConditional). Several implementations of I that configuration chooses between
/// are each a keel::Alternative<I> and a keel::Conditional; a dependent that requires I
/// fails at startup if none (or several) is enabled.
///
/// Only components that derive from keel::Conditional are asked: a member function called
/// enabled() in any other component is its own. The override must be
/// `bool enabled() const`; with another signature the component stays abstract
/// (AbstractComponent). Deriving privately, or twice, is InaccessibleConditional. An
/// interface that declares its own `virtual bool enabled() const`, implemented by a
/// component that also derives from keel::Conditional, shares one override with it.
class Conditional {
protected:
    /// Whether this component runs. See above.
    virtual bool enabled() const = 0;

    Conditional() = default;
    Conditional(const Conditional&) = default;
    Conditional& operator=(const Conditional&) = default;
    virtual ~Conditional() = default;

private:
    friend class detail::Registry;
};

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

template <class C>
inline constexpr bool isBundle = false;
template <class... Cs>
inline constexpr bool isBundle<Bundle<Cs...>> = true;

template <class List, class T>
struct AppendImpl;
template <class... Ts, class T>
struct AppendImpl<TypeList<Ts...>, T> {
    using type = TypeList<Ts..., T>;
};
template <class List, class T>
using Append = typename AppendImpl<List, T>::type;

// The result of expanding the keel::Bundle<...>s of a compose<...> list: the components,
// and the bundles reached more than once in the tree (each once). A repeated bundle is
// expanded only the first time, so it is reported as one DuplicateBundle<B>, not as a
// duplicate of each of its components.
template <class Components, class RepeatedBundles = TypeList<>>
struct Expanded {
    using components = Components;
    using repeated = RepeatedBundles;
};

// Expands the list left to right, in place: a bundle's components take its position.
// Seen: the bundles expanded so far.
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

// A list without bundles is used as is.
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

// The class of a pointer to member function: `R C::*` also matches `R (C::*)(Args...)`,
// with R deduced as the function type `R(Args...)`.
template <class M>
struct MemberOf;
template <class R, class C>
struct MemberOf<R C::*> {
    using type = C;
};

// How a component uses the type an entry of its keel::Component<...> list names.
// `misnested` is All<Optional<I>>, and `listedProvides` / `listedAlternative`
// keel::Provides<I> / keel::Alternative<I>, which are base classes, not entries
// (ProvidesAsDependency, AlternativeAsDependency), and `listedAsync` keel::AsyncInit or
// keel::AsyncStart, base classes too (AsyncAsDependency); all are reported at compose time.
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

// Separates the entries of a keel::Component<...> list by kind: required, optional,
// ordering only, collections (All<I>, Optional<All<I>>), and the base classes listed by
// mistake (Provides<I>, Alternative<I>, AsyncInit, AsyncStart). `target` is the type the
// entry names (for AsyncInit and AsyncStart, the marker itself).
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

// Optional<T>, After<T> and Optional<All<T>> may name a type that isn't composed, and
// All<T> an interface, but it must be a type that could be one: a class,
// not const or volatile, not another marker (AsyncInit and AsyncStart included). Anything
// else would never match.
template <class T>
inline constexpr bool couldBeComponent =
    std::is_class_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T> && !isDependencyMarker<T>;

}  // namespace detail

// See the declaration above. Empty. I is checked in the constructor rather than in the class
// body: a failed check there would make the derived component invalid, and every later use
// of it would add an error.
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
// The common base of every keel::Alternative<I>: whether a component is an alternative of
// anything, without knowing I (AlternativeNotConditional).
struct AlternativeMarker {};
}  // namespace detail

// See the declaration above. Checked as keel::Provides<I> is.
template <class I>
struct Alternative : detail::AlternativeMarker {
    constexpr Alternative() noexcept {
        static_assert(detail::couldBeComponent<I>,
                      "keel: keel::Alternative<I> needs an interface I that a dependency could name: a class type, "
                      "not const or volatile, not a reference, and not a keel marker (keel::Optional, keel::All, "
                      "...). Write keel::Alternative<Cache>, not keel::Alternative<const Cache>.");
    }
};

namespace detail {

// C is a keel::Conditional that keel can ask: it derives from it publicly and once.
// (Privately or twice is InaccessibleConditional, at compose time.) False for an
// incomplete C.
template <class C>
inline constexpr bool isConditional = std::is_convertible_v<C*, Conditional*>;

// C derives from keel::AsyncInit / keel::AsyncStart publicly and once. (Privately or twice
// is InaccessibleAsync, at compose time.)
template <class C>
inline constexpr bool hasAsyncInit = std::is_convertible_v<C*, AsyncInit*>;
template <class C>
inline constexpr bool hasAsyncStart = std::is_convertible_v<C*, AsyncStart*>;

// A C can be used as an I: I could be a component (above) and is C itself or a public,
// unambiguous base of C. A pointer conversion checks access and ambiguity, and is simply
// false for an incomplete I (C can't derive from it then).
template <class C, class I>
constexpr bool implements() {
    if constexpr (couldBeComponent<I>) {
        return std::is_convertible_v<C*, I*>;
    } else {
        return false;
    }
}

/// Human-readable type name at compile time, without RTTI.
template <class T>
constexpr std::string_view typeName() {
#if defined(__clang__)
    // "std::string_view keel::detail::typeName() [T = Logger]". The name itself may contain
    // ']' and ';' (Arr<int[3]>, a string template argument), so it ends at the last ']'.
    constexpr std::string_view f = __PRETTY_FUNCTION__;
    constexpr auto begin = f.find("T = ") + 4;
    return f.substr(begin, f.rfind(']') - begin);
#elif defined(__GNUC__)
    // "constexpr std::string_view keel::detail::typeName() [with T = Logger;
    // std::string_view = std::basic_string_view<char>]": the name ends where the
    // std::string_view typedef is listed after it (searched from the end, since the name
    // may contain ';' and ']'), or at the last ']' if it isn't listed.
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

// ===========================================================================
// Runtime state of an App: the components in init order, and lookups by type.

struct Binding {
    ComponentBase* owner;
    void* object;      // the component, or its interface subobject; cast back to the bound type only
    bool alternative;  // bound through keel::Alternative<>: chosen at runtime
};

// For keel's own tests only: called on the thread that drives the App before each thread
// keel creates. A test makes it throw std::system_error to simulate a failed creation.
struct TestHooks {
    void (*beforeThread)() = nullptr;
};
inline TestHooks testHooks;

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

// T derives publicly, and once, from one keel::Component<...>. A pointer conversion checks
// access and ambiguity too; no pointer is formed for a non-class T (a reference, a
// function type). An incomplete T is not a component.
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

// Diagnostics. Each failure is a class template named after the mistake, whose template
// arguments name the types involved: MissingDependency<Aggregator, WorkerPool>. A check
// that passes is an empty Passed<...> instead, so a failing one is never instantiated
// unless it is the mistake. The checks run per component (ComponentDiagnosis), which keeps
// the compiler's "in instantiation of" notes short. One mistake, one error.
//
// The same outcomes serve keel::compose<...>, which derives from them (so each failure's
// static_assert fires), and keel::isValid<...>, which only tests isPassed<> on them (which
// never instantiates a failure). So the two can't disagree.
template <class... Context>
struct Passed {};

template <class Outcome>
inline constexpr bool isPassed = false;
template <class... Context>
inline constexpr bool isPassed<Passed<Context...>> = true;

// The outcome of a check, carried as a type without instantiating it (a function that
// returned the failure by value would instantiate it).
template <class Result>
using Outcome = std::type_identity<Result>;

// The duplicate checks of a component's own list; Base is its keel::Component<...>. Its
// constructor instantiates it, and so does compose<...>; it is the same instantiation, so
// a duplicate is one error.
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
inline constexpr bool dependencyComposed = false;  // only read when the check fails
template <class Component, class Dependency>
struct MissingDependency {
    static_assert(dependencyComposed<Component, Dependency>,
                  "keel: missing dependency: the Component in MissingDependency<Component, Dependency> needs "
                  "Dependency, and no composed component is or provides it.");
};

template <class Component, class Interface, class Providers>
inline constexpr bool singleProvider = false;  // only read when the check fails
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
inline constexpr bool unmixedProviders = false;  // only read when the check fails
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
inline constexpr bool anyProvider = false;  // only read when the check fails
template <class Component, class Interface>
struct MissingProvider {
    static_assert(anyProvider<Component, Interface>,
                  "keel: the Component in MissingProvider<Component, Interface> lists keel::All<Interface>, which "
                  "needs at least one provider, and no composed component (other than Component itself) is or "
                  "provides Interface. Compose one, or depend on keel::Optional<keel::All<Interface>> if none is "
                  "fine.");
};

template <class Component, class Interface, class Providers>
inline constexpr bool unconditionalProvider = false;  // only read when the check fails
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
inline constexpr bool unconditionalDependency = false;  // only read when the check fails
template <class Component, class T>
struct RequiredConditional {
    static_assert(unconditionalDependency<Component, T>,
                  "keel: the Component in RequiredConditional<Component, T> requires T, and T is a "
                  "keel::Conditional that configuration may turn off. Depend on keel::Optional<T> instead, and "
                  "use findComponent<T>(), which returns nullptr while T is disabled (or keel::After<T> for "
                  "ordering only).");
};

template <class Component, class Interface, class Provider>
inline constexpr bool unconditionalInterface = false;  // only read when the check fails
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
inline constexpr bool conditionalAlternative = false;  // only read when the check fails
template <class Component>
struct AlternativeNotConditional {
    static_assert(conditionalAlternative<Component>,
                  "keel: the Component in AlternativeNotConditional<Component> derives from keel::Alternative<I>, "
                  "so configuration decides whether it is the one, but it doesn't derive from keel::Conditional. "
                  "Derive from keel::Conditional publicly and override `bool enabled() const`, or derive from "
                  "keel::Provides<I> instead if it is always the one.");
};

template <class Component, class Interface>
inline constexpr bool providesOrAlternative = false;  // only read when the check fails
template <class Component, class Interface>
struct ProvidesAndAlternative {
    static_assert(providesOrAlternative<Component, Interface>,
                  "keel: the Component in ProvidesAndAlternative<Component, Interface> derives from "
                  "keel::Provides<Interface> and from keel::Alternative<Interface>. Keep one: "
                  "keel::Alternative<Interface> if configuration chooses it among other implementations, "
                  "keel::Provides<Interface> if it is always the one.");
};

template <class Component, class Interface>
inline constexpr bool wellNestedAll = false;  // only read when the check fails
template <class Component, class Interface>
struct MisnestedAll {
    static_assert(wellNestedAll<Component, Interface>,
                  "keel: the Component in MisnestedAll<Component, Interface> lists "
                  "keel::All<keel::Optional<Interface>>; write keel::Optional<keel::All<Interface>> for a list "
                  "that may be empty");
};

template <class Component, class T, class Derived>
inline constexpr bool providerDeclared = false;  // only read when the check fails
template <class Component, class T, class Derived>
struct UndeclaredProvider {
    static_assert(providerDeclared<Component, T, Derived>,
                  "keel: the Component in UndeclaredProvider<Component, T, Derived> depends on T, nothing "
                  "composed is or provides T, and Derived derives from T without deriving from keel::Provides<T>. "
                  "Dependencies match by exact type: derive Derived from keel::Provides<T> too (publicly), or "
                  "depend on Derived.");
};

template <class Component, class T, class Derived>
inline constexpr bool subclassDeclared = false;  // only read when the check fails
template <class Component, class T, class Derived>
struct UndeclaredSubclass {
    static_assert(subclassDeclared<Component, T, Derived>,
                  "keel: the Component in UndeclaredSubclass<Component, T, Derived> depends on T, which isn't "
                  "composed. Derived is a subclass of the component T, but keel matches exact types: compose T "
                  "itself, or derive T from keel::Provides<T> (publicly) so its subclasses provide T.");
};

template <class Component, class T, class Derived>
inline constexpr bool providerListed = false;  // only read when the check fails
template <class Component, class T, class Derived>
struct LeftOutOfAll {
    static_assert(providerListed<Component, T, Derived>,
                  "keel: the Component in LeftOutOfAll<Component, T, Derived> lists keel::All<T> (or "
                  "keel::Optional<keel::All<T>>), and Derived derives from T but would be left out: keel matches "
                  "exact types. Derive Derived from keel::Provides<T> too (for a subclass of the component T, "
                  "derive T itself from it), or stop deriving from T.");
};

template <class Component, class Interface>
inline constexpr bool derivesFromInterface = false;  // only read when the check fails
template <class Component, class Interface>
struct NotDerivedFromInterface {
    static_assert(derivesFromInterface<Component, Interface>,
                  "keel: the Component in NotDerivedFromInterface<Component, Interface> derives from "
                  "keel::Provides<Interface> (or keel::Alternative<Interface>) but doesn't derive from "
                  "Interface publicly and unambiguously (or Interface isn't defined at this point)");
};

template <class Component, class Interface>
inline constexpr bool accessibleProvides = false;  // only read when the check fails
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
inline constexpr bool accessibleAlternative = false;  // only read when the check fails
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
inline constexpr bool notListedAlternative = false;  // only read when the check fails
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
inline constexpr bool notListedProvides = false;  // only read when the check fails
template <class Component, class Interface>
struct ProvidesAsDependency {
    static_assert(notListedProvides<Component, Interface>,
                  "keel: the Component in ProvidesAsDependency<Component, Interface> lists keel::Provides<Interface> "
                  "in its keel::Component<...>, which lists only what the component needs. Inherit it instead, next "
                  "to keel::Component<...>: class Component : public keel::Component<...>, public "
                  "keel::Provides<Interface>, public Interface");
};

// Every provider of one interface that derives from keel::Provides<Interface> but can't be
// used as one: each failure is an error (one per mistaken provider).
template <class... Failures>
struct ProviderFailures : Failures... {};

template <class Component, class Entry>
inline constexpr bool validEntry = false;  // only read when the check fails
template <class Component, class Entry>
struct InvalidDependency {
    static_assert(validEntry<Component, Entry>,
                  "keel: the Component in InvalidDependency<Component, Entry> lists Entry (T, keel::Optional<T>, "
                  "keel::After<T> or keel::All<T>), and T can never be a component or an interface: T must be "
                  "a class type, not a reference, not const/volatile, and not itself keel::Optional, "
                  "keel::After, keel::Provides, keel::Alternative or keel::All");
};

template <class Component, class Entry>
inline constexpr bool asyncMarkerOnItsOwn = false;  // only read when the check fails
template <class Component, class Entry>
struct MisplacedAsync {
    static_assert(asyncMarkerOnItsOwn<Component, Entry>,
                  "keel: the Component in MisplacedAsync<Component, Entry> lists keel::AsyncInit or "
                  "keel::AsyncStart inside Entry. They aren't dependencies but base classes: class C : public "
                  "keel::Component<Config>, public keel::AsyncInit");
};

template <class Component, class Marker>
inline constexpr bool asyncNotListed = false;  // only read when the check fails
template <class Component, class Marker>
struct AsyncAsDependency {
    static_assert(asyncNotListed<Component, Marker>,
                  "keel: the Component in AsyncAsDependency<Component, Marker> lists the Marker (keel::AsyncInit or "
                  "keel::AsyncStart) in its keel::Component<...>, which lists only dependencies. Inherit it "
                  "instead, next to keel::Component<...>: class Component : public keel::Component<...>, public "
                  "Marker");
};

template <class Component, class Bundle>
inline constexpr bool notABundle = false;  // only read when the check fails
template <class Component, class Bundle>
struct BundleAsDependency {
    static_assert(notABundle<Component, Bundle>,
                  "keel: the Component in BundleAsDependency<Component, Bundle> lists a keel::Bundle<...> in its "
                  "keel::Component<...> (as T, keel::Optional<T>, keel::After<T>, ...). Bundles only group "
                  "components for compose<...>: list the components you use, each on its own.");
};

template <class Bundle>
inline constexpr bool bundleOnce = false;  // only read when the check fails
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

// Depth-first searches over the unplaced components, following the edges forward (i
// depends on j) or backward. Each search marks the components it reaches that no earlier
// search reached, and appends each to `finished` once every component it leads to is
// done. Iterative, and each row is scanned once over all searches: O(N²).
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

// The components that are on a cycle: the unplaced ones in a strongly connected component
// of two or more, or that depend on themselves. Components that only depend on a cycle,
// or sit between two cycles, are left out. Only evaluated when there is a cycle.
// Kosaraju's algorithm, O(N²).
template <std::size_t N>
constexpr std::array<bool, N> componentsOnCycles(const Graph<N>& graph, const std::array<bool, N>& placed) {
    // Pass 1: the order in which forward searches finish the components.
    CycleSearch<N> first(graph, placed, true);
    for (std::size_t i = 0; i < N; ++i) {
        if (!placed[i] && first.mark[i] == 0) first.search(i);
    }
    // Pass 2: backward searches, latest finished first; each reaches exactly one strongly
    // connected component.
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

// How a component C derives from keel::Provides<I>. C++20 can't list the bases of a class,
// so keel asks this for each I something refers to. `declared`: publicly and once, and C
// derives from I publicly and unambiguously too. `notDerived`: publicly and once, but C
// can't be used as an I (NotDerivedFromInterface). `inaccessible`: privately, protectedly
// or more than once (InaccessibleProvides). Pointer conversions check access and ambiguity
// (and are false for an incomplete I). Traits only: it is evaluated for every component
// and every type something refers to.
// (The mistakes come last: `>= notDerived` tests for them.)
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

// C derives from keel::Provides<I> or keel::Alternative<I> publicly and once (whether or not
// it can be used as an I).
template <class C, class I>
inline constexpr bool providesBase = provision<C, I> == Provision::declared || provision<C, I> == Provision::notDerived;
template <class C, class I>
inline constexpr bool alternativeBase =
    alternativeProvision<C, I> == Provision::declared || alternativeProvision<C, I> == Provision::notDerived;

// C derives from keel::Provides<I> or keel::Alternative<I> but can't be used as an I, or
// from both. Reported per mistaken provider, the first that applies.
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

// C is an alternative of T: chosen at runtime among the others.
template <class T, class C>
inline constexpr bool isAlternative = alternativeBase<C, T> || contains<T, ListedAlternativesOf<C>>;

// A dependency on T resolves to C: C is T, or derives from keel::Provides<T> or
// keel::Alternative<T> (publicly). Several such C are ambiguous for a dependency on one T,
// unless each is a keel::Alternative<T>. (A keel::Provides<T> or keel::Alternative<T> listed
// by mistake counts too, so that ProvidesAsDependency is the one error, and so does a C that
// can't be used as a T, so that NotDerivedFromInterface is.)
template <class T, class C>
inline constexpr bool isProvider =
    std::is_same_v<T, C> || providesBase<C, T> || alternativeBase<C, T> || contains<T, ListedOf<C>>;

// An entry that refers to a type keel resolves: a dependency (T, Optional<T>, After<T>,
// All<T>, Optional<All<T>>) on a type that could be a component. These, and the root's
// getComponent<T>() and run(&T::f), are the types keel asks the components about.
template <class Entry>
inline constexpr bool isDependencyEntry =
    (Split<Entry>::use == Use::required || Split<Entry>::use == Use::optional ||
     Split<Entry>::use == Use::ordering || Split<Entry>::use == Use::all || Split<Entry>::use == Use::optionalAll) &&
    couldBeComponent<typename Split<Entry>::target> && !isBundle<typename Split<Entry>::target>;
// The entries the App binds at runtime: the dependencies.
template <class Entry>
inline constexpr bool isReferral = isDependencyEntry<Entry>;

// The entry is a dependency on T.
template <class T, class Entry>
inline constexpr bool isDependencyOn = isReferral<Entry> && std::is_same_v<typename Split<Entry>::target, T>;
template <class T, class Entries>
inline constexpr bool hasDependencyOn = false;
template <class T, class... Entries>
inline constexpr bool hasDependencyOn<T, TypeList<Entries...>> = (isDependencyOn<T, Entries> || ...);
// C's list depends on T, and compose<...> checks it (a list with a duplicate isn't checked
// further).
template <class T, class C>
inline constexpr bool dependsOn = uniqueEntriesOf<C> && hasDependencyOn<T, EntriesOf<C>>;

// The first entry of a list that depends on T, or void.
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

    // Which components a dependency on T resolves to: T itself, and every component that
    // derives from keel::Provides<T> or keel::Alternative<T>.
    template <class T>
    static constexpr Row providers{isProvider<T, Cs>...};
    template <class T>
    static constexpr std::size_t providerCount = count(providers<T>);

    template <class T, std::size_t... Is>
    static auto providersOf(std::index_sequence<Is...>)
        -> Concat<std::conditional_t<providers<T>[Is], TypeList<At<Is>>, TypeList<>>...>;
    // Every component a dependency on T resolves to, in compose<...> order.
    template <class T>
    using Providers = decltype(providersOf<T>(std::make_index_sequence<size>{}));

    // Several providers of T are fine for a dependency on one T if each of them is a
    // keel::Alternative<T>: one is chosen at runtime. (So is a single alternative.)
    template <class T>
    static constexpr Row alternatives{isAlternative<T, Cs>...};
    template <class T>
    static constexpr std::size_t alternativeCount = count(alternatives<T>);
    template <class T>
    static constexpr bool chosenAtRuntime = providerCount<T> > 0 && alternativeCount<T> == providerCount<T>;
    // Some providers of T are alternatives and some aren't: reported once per T as
    // MixedProviders when something needs one T, instead of once per place that needs it.
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

    // The providers of T in init order, by position in the compose<...> list: what a
    // lookup of T finds (the App binds them).
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

    // The components that derive from keel::Provides<T> or keel::Alternative<T> but can't be
    // used as a T, or derive from both. Each is reported once: at the first dependency on T,
    // in compose<...> order, else at the root's getComponent<T>() or run(&T::f). The other
    // dependencies on T pass, since what they would report (a missing or
    // ambiguous T, ...) follows from the mistaken provider.
    template <class T>
    static constexpr Row broken{brokenProvider<Cs, T>...};
    template <class T>
    static constexpr std::size_t brokenCount = count(broken<T>);
    template <class T, std::size_t... Is>
    static auto brokenOf(std::index_sequence<Is...>)
        -> Concat<std::conditional_t<broken<T>[Is], TypeList<ProviderFailure<At<Is>, T>>, TypeList<>>...>;
    template <class T>
    using Broken = typename ProviderFailuresOf<decltype(brokenOf<T>(std::make_index_sequence<size>{}))>::type;

    // The components whose list depends on T.
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
    // For the root's getComponent<T>() and run(&T::f): the broken providers of T, unless
    // compose<...> reports them already.
    template <class T>
    using RootBroken = std::conditional_t<(brokenCount<T> == 0 || dependentCount<T> > 0), Passed<T>, Broken<T>>;

    // The providers of T other than C: what C's After<T> and All<T> refer to.
    template <class C, class T>
    static constexpr Row others{(!std::is_same_v<C, Cs> && isProvider<T, Cs>)...};
    // Those that are always there: not keel::Conditional.
    template <class C, class T>
    static constexpr Row unconditionalOthers{(!std::is_same_v<C, Cs> && isProvider<T, Cs> && !isConditional<Cs>)...};
    template <class C, class T, std::size_t... Is>
    static auto othersOf(std::index_sequence<Is...>)
        -> Concat<std::conditional_t<others<C, T>[Is], TypeList<At<Is>>, TypeList<>>...>;
    template <class C, class T>
    using Others = decltype(othersOf<C, T>(std::make_index_sequence<size>{}));

    // The components other than C that derive from T without being a provider of it: for a
    // dependency of C on T, the likely forgotten keel::Provides<T>.
    template <class C, class T>
    static constexpr Row derived{(!std::is_same_v<C, Cs> && !isProvider<T, Cs> && implements<Cs, T>())...};
    template <class C, class T, std::size_t... Is>
    static auto derivedOf(std::index_sequence<Is...>)
        -> Concat<std::conditional_t<derived<C, T>[Is], TypeList<At<Is>>, TypeList<>>...>;
    template <class C, class T>
    using Derived = decltype(derivedOf<C, T>(std::make_index_sequence<size>{}));

    // The check of one entry of C's keel::Component<...> list, as written (T, Optional<T>,
    // After<T>, All<T> or Optional<All<T>>, and the mistakes): a
    // Passed<...> if fine, else the named failure (see above), wrapped in an Outcome<>.
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
            // Nothing is found as T, or, for All<T>, a component may be left out: one that
            // derives from T without providing it is the likely mistake.
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
            // One provider (several would be ambiguous), which may be turned off.
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

    // The components C is ordered after because of one entry: the provider of a dependency,
    // or every alternative, so that the choice is made first (none if there are several
    // providers otherwise: that is reported as ambiguous, not as a cycle too), and for
    // After<T>, All<T> and Optional<All<T>> every provider of T other than C itself.
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

    template <std::size_t... Is>
    static auto cycleOf(std::index_sequence<Is...>)
        -> Concat<std::conditional_t<onCycle[Is], TypeList<At<Is>>, TypeList<>>...>;
    using Cycle = decltype(cycleOf(std::make_index_sequence<size>{}));

    template <std::size_t... Is>
    static auto sortedOf(std::index_sequence<Is...>) -> TypeList<At<order.index[Is]>...>;
    using Sorted = std::conditional_t<acyclic, decltype(sortedOf(std::make_index_sequence<size>{})), TypeList<Cs...>>;
};

// Checks of the compose<...> list itself, each naming the offending type.
template <class T>
inline constexpr bool isComponentType = false;  // only read when the check fails
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

// The pack is always empty; it only makes the assertion depend on a template argument.
template <class... None>
struct EmptyComposition {
    static_assert(sizeof...(None) > 0, "keel: compose<...> needs at least one component");
};

template <class T, bool component = derivesFromComponent<T>>
inline constexpr bool abstractComponent = false;
template <class T>
inline constexpr bool abstractComponent<T, true> = std::is_abstract_v<T>;

template <class T>
inline constexpr bool accessibleAsync = false;
template <class T>
struct InaccessibleAsync {
    static_assert(accessibleAsync<T>,
                  "keel: the T in InaccessibleAsync<T> derives from keel::AsyncInit or keel::AsyncStart privately, "
                  "protectedly or more than once, so keel can't see it. Derive from it publicly, once: class C : "
                  "public keel::Component<...>, public keel::AsyncInit");
};

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
        std::conditional_t<
            earlier<I> == 0 && !derivesFromComponent<C>, NotAComponent<C>,
            std::conditional_t<
                earlier<I> == 0 && inaccessibleConditional<C>, InaccessibleConditional<C>,
                std::conditional_t<earlier<I> == 0 && inaccessibleAsync<C>, InaccessibleAsync<C>,
                                   std::conditional_t<earlier<I> == 0 && abstractComponent<C>, AbstractComponent<C>,
                                                      Passed<C, std::integral_constant<std::size_t, I>>>>>>>;
};

// Each check is written once, as an outcome type; each group of checks has a struct that
// derives from the outcomes (compose<...>: a failure is instantiated, so its static_assert
// fires) and a constant that tests them (isValid<...>: nothing is instantiated).

template <class List, class Indices>
struct ListDiagnosis;
template <class... Cs, std::size_t... Is>
struct ListDiagnosis<ListInfo<Cs...>, std::index_sequence<Is...>> : ListInfo<Cs...>::template Check<Cs, Is>... {};
template <class List, class Indices>
inline constexpr bool listPasses = false;
template <class... Cs, std::size_t... Is>
inline constexpr bool listPasses<ListInfo<Cs...>, std::index_sequence<Is...>> =
    (isPassed<typename ListInfo<Cs...>::template Check<Cs, Is>> && ...);

// The checks of one component C's list, each entry as written.
template <class Info, class C, class Entries = EntriesOf<C>>
struct ComponentDiagnosis;
// An alternative of anything must be a keel::Conditional; checked per component, whether
// or not something refers to its interfaces. The tag of the passed check is a type no list
// entry can name, so that it never repeats an entry's Passed<C, Entry> base.
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

// A component whose own list has a duplicate is not checked further: the duplicate is the
// one error.
template <class Info, class C>
using ComponentOutcome = std::conditional_t<uniqueEntriesOf<C>, ComponentDiagnosis<Info, C>,
                                            ComponentListCheck<ComponentOf<C>>>;
template <class Info, class C>
inline constexpr bool componentPasses = uniqueEntriesOf<C> && entriesPass<Info, C>;

// Every component's checks at once, so each mistake in each component is reported. Only on
// a well-formed list (naming the checks reads each component's list); otherwise the list's
// own check fails, and that is the error.
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

// The analysis of a compose<...> list: its components (bundles already expanded), and the
// bundles reached more than once. The graph analysis only runs on a list of distinct,
// concrete components, so a list that isn't one reports that and nothing else.
template <class Expansion>
struct Analysis;
template <class... Cs, class Repeated>
struct Analysis<Expanded<TypeList<Cs...>, Repeated>> {
    using List = ListInfo<Cs...>;
    using Indices = std::index_sequence_for<Cs...>;
    static constexpr bool wellFormed = listPasses<List, Indices>;
    using Info = Composition<std::conditional_t<wellFormed, TypeList<Cs...>, TypeList<>>>;

    // The outcomes, by group.
    using OfBundles = BundleDiagnosis<Repeated>;
    using OfList = ListDiagnosis<List, Indices>;
    using OfComponents = std::conditional_t<wellFormed, ComponentsDiagnosis<Info, TypeList<Cs...>>, Passed<>>;
    using NonEmpty = std::conditional_t<(sizeof...(Cs) > 0), Passed<>, EmptyComposition<>>;
    using Acyclic = std::conditional_t<Info::acyclic, Passed<>, DependencyCycle<typename Info::Cycle>>;

    // Whether every outcome above is Passed: what keel::isValid<...> returns.
    static constexpr bool ok = std::is_same_v<Repeated, TypeList<>> && wellFormed &&
                               componentsPass<wellFormed, Info, TypeList<Cs...>> && isPassed<NonEmpty> &&
                               isPassed<Acyclic>;
};

}  // namespace detail

/// The name keel uses for the type T: in App::initOrder(), keel::Timing, App::toDot(),
/// componentName() and error messages. The type as the compiler spells it
/// (`Logger`, `edge::Imu<edge::Front>`), which may differ between compilers.
template <class T>
constexpr std::string_view typeName() noexcept {
    return detail::typeName<T>();
}

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

// ===========================================================================
// keel::App and keel::compose

namespace detail {

template <class Expansion>
class AppImpl;

// Appends s for a quoted DOT string: a backslash or a double quote gets a backslash (in a
// label, DOT would read \n, \l, ... as escapes, and \" as a quote).
inline void appendDotEscaped(std::string& out, std::string_view s) {
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
}

// The arrows of one entry of a component's list, from node n<from> to each n<j> with
// targets[j]; labelled with the target type when it isn't the component's own type
// (ids[j]), i.e. it resolves through an interface. Not a template, so that toDot() on a
// large app compiles one loop, not one per entry.
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

// Diagnostics of App::getComponent<T>().
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

// Diagnostics of run(fn), each named after the mistake.
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
// A mistake of the composition that run(&C::f) runs into (MixedProviders, or providers of
// C that can't be used as one: see above); as a base, it is reported once with the others.
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

// A class whose operator() is a single non-template function (not a generic lambda).
template <class F>
inline constexpr bool hasPlainCallOperator = requires { &F::operator(); };


// The composition (bundles already expanded) is checked when the class is instantiated.
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
        if constexpr (anyAsync(typename Info::Sorted{})) {
            registry_.setHasAsync();
            addDependencies();
        }
    }

    // Not copyable or movable: components hold pointers to each other. (Without the
    // deleted move, the constructor template above would be picked for a move.)
    AppImpl(const AppImpl&) = delete;
    AppImpl(AppImpl&&) = delete;
    AppImpl& operator=(const AppImpl&) = delete;
    AppImpl& operator=(AppImpl&&) = delete;

    ~AppImpl() { registry_.deinit(); }

    /// Calls init() on every component, in dependency order; on a keel::Conditional, asks
    /// enabled() first (a disabled component is skipped from then on). If one throws, or a
    /// dependency on keel::Alternative<>s finds none or several enabled (keel::Error), the
    /// components already initialized are deinitialized in reverse, and the exception
    /// propagates. Callable once. Components with keel::AsyncInit run on threads of their
    /// own (see there); init() returns or throws after they all finished.
    void init() { registry_.init(); }
    /// setAsync(false) runs every component on the calling thread, in init order, as if
    /// none derived from keel::AsyncInit or keel::AsyncStart: to debug, to reproduce a run, or to
    /// narrow down a race. Call it before init() (after, it throws keel::Error). Default true.
    void setAsync(bool async) { registry_.setAsync(async); }
    /// Calls start() on every enabled component, in dependency order. Requires a successful
    /// init(). If one throws, the components already started are stopped in reverse, then every
    /// component is deinitialized in reverse, and the exception propagates. Callable once.
    /// Components with keel::AsyncStart run on threads of their own, as for keel::AsyncInit.
    void start() { registry_.start(); }
    /// Calls stop() on every started component, in reverse order. Does nothing if the app
    /// isn't started.
    void stop() noexcept { registry_.stop(); }
    /// Stops the app if it is started, then calls deinit() on every component, in reverse
    /// order. Does nothing the second time. The destructor calls it too.
    void deinit() noexcept { registry_.deinit(); }

    /// For the composition root: a component of this App, or an interface that exactly one
    /// of them Provides<>. Usable at any time, also before init(); after init(), a
    /// keel::Conditional that is disabled throws keel::Error (the root may need it before
    /// init() to configure it, so this isn't a compile error). Two providers of T (also a
    /// component T plus a subclass that inherits its keel::Provides<T>) is a compile error,
    /// as for a dependency.
    /// An interface whose providers are keel::Alternative<T> gets the one init() chose: it
    /// throws keel::Error before init(), and if none or several are enabled.
    template <class T>
    T& getComponent() {
        constexpr std::size_t providers = Info::template providerCount<T>;
        constexpr bool chosenAtRuntime = Info::template chosenAtRuntime<T>;
        using Providers = typename Info::template Providers<T>;
        using Provider = FirstOr<Providers, void>;
        // On an invalid compose<...> list, that is the one error. A provider of T that can't
        // be used as one (InaccessibleProvides, ...) is reported here unless compose<...>
        // reports it.
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
        if constexpr (chosenAtRuntime && implements<Provider, T>()) {  // else an error above
            // The alternatives, in init order. Built here: T may have no bindings, if no
            // component depends on it. On the stack, so the lookup doesn't allocate.
            const auto bindings = alternativeBindings<T>(std::make_index_sequence<providers>{});
            return *static_cast<T*>(registry_.resolve(bindings, typeName<T>(), {}, true)->object);
        } else if constexpr (providers == 1 && implements<Provider, T>()) {
            // Before init(), even if it is a keel::Conditional: the root may configure it.
            Provider& provider = storage_.template get<Provider>();
            registry_.checkEnabled(provider, typeName<T>());
            return provider;
        } else {
            throw Error("keel: unreachable");
        }
    }

    /// The names of the components in init order (construction order too; stop(),
    /// deinit() and destruction run in reverse). Known at compile time, so it can be
    /// checked in a test without running anything:
    ///   static_assert(ServerApp::initOrder()[0] == "Config");
    /// Disabled keel::Conditional components are included (enabled() is asked at runtime);
    /// timings() shows the calls that ran. With keel::AsyncInit and keel::AsyncStart, components with no
    /// dependency path between them may run in another order, or at the same time; each
    /// still runs after the components it is ordered after. Names are keel::typeName<T>():
    /// the type names as the compiler spells them (`Logger`, `edge::Imu<edge::Front>`),
    /// which may differ between compilers.
    static constexpr std::array<std::string_view, Info::size> initOrder() noexcept {
        return namesOf(typename Info::Sorted{});
    }

    /// How long each lifecycle call took, in the order the calls finished (with
    /// keel::AsyncInit or keel::AsyncStart, calls on different threads overlap): one entry
    /// per init(), start(), stop() and deinit() that ran on a component. A disabled
    /// component has none (its enabled() isn't timed), nor does one whose init() never ran
    /// because a dependency on keel::Alternative<>s found none or several enabled. A call
    /// that threw is recorded with `failed`, followed by the rollback's stop() and deinit()
    /// calls. The view stays valid as long as the App, but
    /// holds the entries recorded when timings() was called; call it again later for more.
    /// The destructor's stop() and deinit() can't be seen: call deinit() first, then read
    /// the timings (with keel::run, read them inside fn, which runs before the shutdown).
    /// Call it from the thread that drives the App (not from a component's hook, which may
    /// run while other threads record).
    ///   app.init();
    ///   for (const keel::Timing& t : app.timings())
    ///       std::cout << keel::toString(t.phase) << ' ' << t.component << ' '
    ///                 << std::chrono::duration<double, std::milli>(t.duration).count() << " ms\n";
    std::span<const Timing> timings() const noexcept { return registry_.timings(); }

    /// The dependency graph in Graphviz DOT format, to render with Graphviz:
    ///   std::ofstream("graph.dot") << app.toDot();   // then: dot -Tsvg graph.dot -o graph.svg
    /// One box per component, in init order, with the interfaces it provides ("provides I",
    /// "alternative I") and its async base classes ("async init", "async start"). Only the interfaces some
    /// component of the App refers to are shown (keel::Provides<I> is a base class, and C++20
    /// can't list a class's bases). An arrow goes from a component to each component it is
    /// ordered after because of one entry of its list: plain for a required dependency, dashed
    /// for keel::Optional, dotted with an open head for keel::After, bold for keel::All,
    /// bold and dashed for keel::Optional<keel::All>; labelled with the interface when it
    /// resolves through one. Two entries that resolve to the same component (T, and an
    /// interface T provides) give two arrows. Arrows point to the dependencies, which are drawn
    /// to the right: init order reads right to left. After init(), disabled components are gray
    /// and marked "(disabled)"; their arrows are still drawn.
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
    // init, start, fn, then stop and deinit, also if fn throws. Self is the App, which
    // run(fn) passes to fn.
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

    // start, then fn(self), fn(), or a member function of one of the components
    // (&Server::serve), whose target is resolved first: an alternative that isn't chosen
    // throws before anything starts. A mistake gives exactly one error: one of the diagnostics above, or
    // the compiler's own error inside a generic callable.
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
                // One provider, which may be turned off (alternatives are chosen at runtime).
                return RunTargetConditional<C, FirstOr<typename Info::template Providers<C>, void>>{};
            } else if constexpr (!std::is_invocable_v<F, C&>) {
                return RunTargetNotCallable<F>{};
            } else {
                C& target = self.template getComponent<C>();
                registry.start();
                return (target.*fn)();
            }
        } else if constexpr (std::is_invocable_v<Fn>) {
            // Callable with no arguments. If it also takes the App, it gets the App.
            registry.start();
            if constexpr (std::is_invocable_v<Fn, Self&>) {
                return std::forward<Fn>(fn)(self);
            } else {
                return std::forward<Fn>(fn)();
            }
        } else if constexpr (std::is_class_v<F> && !hasPlainCallOperator<F>) {
            // A generic lambda (or an overloaded function object): called with the App
            // directly, so an error in its body is reported once, by the compiler, instead
            // of turning into "not callable".
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

    // Calls visit.template operator()<T>() once for each type T that a component depends
    // on, in the order the components and their entries first name it.
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

    // The bindings of T's providers, which are alternatives, in init order.
    template <class T, std::size_t... Js>
    std::array<Binding, sizeof...(Js)> alternativeBindings(std::index_sequence<Js...>) {
        return {bindingOf<T, typename Info::template At<Info::template providersInOrder<T>[Js]>>()...};
    }
    // (A provider that can't be used as a T is a compile error; it gets no object.)
    template <class T, class C>
    Binding bindingOf() {
        C& component = storage_.template get<C>();
        void* object = nullptr;
        if constexpr (implements<C, T>()) object = static_cast<void*>(static_cast<T*>(&component));
        return Binding{&static_cast<ComponentBase&>(component), object, isAlternative<T, C>};
    }

    // Binds T to its providers in init order, each to its T subobject. (A provider that
    // can't be used as a T is a compile error, and not bound.)
    template <class T, std::size_t... Js>
    void bindProviders(std::index_sequence<Js...>) {
        registry_.beginBindings(typeId<T>());
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
    // The ordering edges of the graph the init order comes from, by init position: what the
    // async scheduler waits for. A component's dependencies all come before it.
    void addDependencies() {
        for (std::size_t k = 0; k < Info::size; ++k) {
            for (std::size_t m = 0; m < k; ++m) {
                if (Info::graph[Info::order.index[k]][Info::order.index[m]]) registry_.addDependency(k, m);
            }
        }
    }

    template <class... Sorted>
    static constexpr std::array<std::string_view, sizeof...(Sorted)> namesOf(TypeList<Sorted...>) noexcept {
        return {typeName<Sorted>()...};
    }

    // toDot(): node n<i> is the i-th component of the compose<...> list (bundles expanded).
    // Nodes, then edges, are written in init order: Ks are init positions.
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
    // Appends "provides T" (or "alternative T") to the label of each provider of T.
    template <class T, std::size_t... Js>
    static void dotInterfaces([[maybe_unused]] std::vector<std::string>& interfaces, std::index_sequence<Js...>) {
        (dotInterface<T, Info::template providersInOrder<T>[Js]>(interfaces), ...);
    }
    template <class T, std::size_t i>
    static void dotInterface(std::vector<std::string>& interfaces) {
        using C = typename Info::template At<i>;
        // A component that provides itself shows nothing; one that can't be used as a T is a
        // compile error.
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
    // The edges of one entry: the components edgesOf() orders C after for it.
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

}  // namespace detail

/// A statically composed application. Components (and keel::Bundle<...>s of them) in any
/// order: keel checks the composition at compile time and derives the order from the
/// declared dependencies. Drive it with run(fn), or step by step: init(), start(), ...,
/// stop(), deinit().
template <class... Components>
class App : public detail::AppImpl<detail::ExpandBundles<Components...>> {
    using Base = detail::AppImpl<detail::ExpandBundles<Components...>>;

public:
    using Base::Base;

    /// Runs the whole lifecycle: init(), start(), then fn, then stop() and deinit(), also
    /// if fn throws. Returns what fn returns. fn is one of:
    ///   app.run([] { waitForShutdown(); });          // a callable with no arguments
    ///   app.run([](ServerApp& running) { ... });     // a callable taking the App
    ///   app.run(&Server::serve);                     // a member function of a component,
    ///                                                // called on that component
    ///   app.run(&Shell::serve);                      // a member function of an interface,
    ///                                                // called on its one provider
    /// A callable that accepts both gets the App. Name the App type to write the parameter
    /// without templates:
    ///   using ServerApp = keel::App<Server, Config>;
    ///   ServerApp app;
    ///   app.run([](ServerApp& running) { running.getComponent<Server>().serve(); });
    /// If init() or start() throws, the exception propagates after the rollback, and fn
    /// doesn't run. Calling run() after init() throws keel::Error. A reference returned by fn
    /// refers to an object that is already deinitialized when run() returns.
    /// run(&C::f) resolves C after init() and before start(). A C that is (or is provided by)
    /// a keel::Conditional doesn't compile (RunTargetConditional). On an interface whose
    /// providers are keel::Alternative<>s, f is called on the enabled one; if none is
    /// enabled, it throws keel::Error (after deinit(); nothing is started).
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
/// default-constructed. The list may contain keel::Bundle<...>s.
template <class... Components, class... Args>
App<Components...> compose(Args&&... args) {
    return App<Components...>(std::forward<Args>(args)...);
}

/// Whether keel::compose<Components...> would pass keel's compile-time checks, as a
/// constant: true or false, never a compile error. For tests of compositions that must
/// fail, and for choosing between compositions (if constexpr):
///   static_assert(!keel::isValid<Core, HttpServer>);  // HttpServer needs a Database
/// It runs every check compose<...> runs on the list and the declared dependencies: types
/// that aren't components, duplicates, missing or ambiguous dependencies, cycles, and so on.
/// It doesn't see how the components use them (a getComponent<T>() for an undeclared T),
/// nor what constructing and destroying them needs: constructor arguments, a missing
/// default constructor, a private or deleted destructor. For a guard that names the
/// mistake when it fails, use keel::requireValid.
/// Every component must be defined (complete) where isValid is first used with it: the
/// answer is fixed then, and an incomplete type counts as "not a component" from then on,
/// also in later compositions in the same file.
template <class... Components>
inline constexpr bool isValid = detail::Analysis<detail::ExpandBundles<Components...>>::ok;

/// A composition guard for headers: true, or the compile error compose<...> would give,
/// naming the mistake (never false). Next to the compositions, so every build variant is
/// checked on every build, also the ones no binary of the current build uses:
///   using Core = keel::Bundle<Config, Logger>;
///   static_assert(keel::requireValid<Core, Database, HttpServer>);      // production
///   static_assert(keel::requireValid<Core, FakeDatabase, HttpServer>);  // tests
/// It checks what keel::isValid checks.
template <class... Components>
inline constexpr bool requireValid = sizeof(App<Components...>) > 0;

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
