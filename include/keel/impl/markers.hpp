// keel/impl/markers.hpp: the public vocabulary: keel::Error, Phase and Timing, the
// dependency markers (Optional, After, All, Bundle) and the base classes (Provides, Alternative,
// AsyncInit, AsyncStart, Conditional).
//
// Part of keel.hpp: tools/amalgamate.py joins the parts into the single header.
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include <chrono>
#include <stdexcept>
#include <string_view>

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

}  // namespace keel
