// keel/impl/app.hpp: keel::App, keel::compose, keel::isValid, keel::requireValid and
// keel::run.
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
#include "component_base.hpp"
#include "registry.hpp"
#include "diagnostics.hpp"
#include "graph.hpp"
#include "analysis.hpp"
#include "component.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace keel {

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

}  // namespace keel
