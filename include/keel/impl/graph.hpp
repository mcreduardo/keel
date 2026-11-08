// keel/impl/graph.hpp: the dependency graph (init order, cycles) and the providers and
// checks of each entry of a composition (Composition).
//
// Part of keel.hpp: tools/amalgamate.py joins the parts into the single header.
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include "markers.hpp"
#include "type_list.hpp"
#include "split.hpp"
#include "component_base.hpp"
#include "diagnostics.hpp"

#include <array>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace keel {

namespace detail {

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

}  // namespace detail

}  // namespace keel
