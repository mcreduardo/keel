// keel::App, compose, isValid, requireValid and run. See docs/reference.md.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include "markers.hpp"
#include "introspection.hpp"
#include "type_list.hpp"
#include "type_name.hpp"
#include "lists.hpp"
#include "component.hpp"
#include "component_info.hpp"
#include "diagnostics.hpp"
#include "composition.hpp"
#include "analysis.hpp"
#include "registry.hpp"
#include "storage.hpp"
#include "lifecycle.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace keel {

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
