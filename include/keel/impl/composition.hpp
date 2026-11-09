// A compose<...> list as a whole (Composition): providers, entry checks, graph and init order.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include "markers.hpp"
#include "type_list.hpp"
#include "lists.hpp"
#include "component_info.hpp"
#include "diagnostics.hpp"
#include "graph.hpp"

#include <array>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace keel {

namespace detail {

// Asked for each I something refers to, since C++20 can't list a class's bases. Traits only:
// evaluated for every component and referred type. The mistakes come last (`>= notDerived`).
enum class Provision : unsigned char { none, declared, notDerived, inaccessible };
// Marker is keel::Provides or keel::Alternative.
template <template <class> class Marker, class C, class I>
constexpr Provision provisionVia() {
    if constexpr (!couldBeComponent<I>) {
        return Provision::none;
    } else if constexpr (std::is_convertible_v<C*, Marker<I>*>) {
        return std::is_convertible_v<C*, I*> ? Provision::declared : Provision::notDerived;
    } else {
        return hiddenBase<Marker<I>, C> ? Provision::inaccessible : Provision::none;
    }
}
template <class C, class I>
inline constexpr Provision provision = provisionVia<Provides, C, I>();
template <class C, class I>
inline constexpr Provision alternativeProvision = provisionVia<Alternative, C, I>();

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

template <class T, class Entry>
inline constexpr bool isDependencyOn = isDependencyEntry<Entry> && std::is_same_v<typename Split<Entry>::target, T>;
template <class T, class Entries>
inline constexpr bool hasDependencyOn = false;
template <class T, class... Entries>
inline constexpr bool hasDependencyOn<T, TypeList<Entries...>> = (isDependencyOn<T, Entries> || ...);
// A list with a duplicate isn't checked further.
template <class T, class C>
inline constexpr bool dependsOn = uniqueEntriesOf<C> && hasDependencyOn<T, EntriesOf<C>>;

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

    // The entries of List (default: the components) whose bit in keep is set.
    template <Row keep, class List = TypeList<Cs...>>
    using Keep = typename KeepIf<List, keep, std::make_index_sequence<size>>::type;
    template <class T>
    using Providers = Keep<providers<T>>;

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
    template <class T>
    static constexpr Row plainProviders{(isProvider<T, Cs> && !isAlternative<T, Cs>)...};
    template <class T>
    using Mixed = MixedProviders<T, Keep<plainProviders<T>>, Keep<alternatives<T>>>;

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
    template <class T>
    using Broken = typename ProviderFailuresOf<Keep<broken<T>, TypeList<ProviderFailure<Cs, T>...>>>::type;

    template <class T>
    static constexpr Row dependents{dependsOn<T, Cs>...};
    template <class T>
    static constexpr std::size_t dependentCount = count(dependents<T>);
    static constexpr std::size_t first(const Row& row) {
        std::size_t i = 0;
        while (i < size && !row[i]) ++i;
        return i;
    }
    template <class C>
    static constexpr std::size_t index = first(Row{std::is_same_v<C, Cs>...});
    // C reports them; its list is unique, so the entry checked is its only one on T.
    template <class C, class T>
    static constexpr bool reportsBroken = first(dependents<T>) == index<C>;
    template <class T>
    using RootBroken = std::conditional_t<(brokenCount<T> == 0 || dependentCount<T> > 0), Passed<T>, Broken<T>>;

    template <class C, class T>
    static constexpr Row others{(!std::is_same_v<C, Cs> && isProvider<T, Cs>)...};
    template <class C, class T>
    static constexpr Row unconditionalOthers{(!std::is_same_v<C, Cs> && isProvider<T, Cs> && !isConditional<Cs>)...};
    template <class C, class T>
    using Others = Keep<others<C, T>>;

    // For a dependency of C on T: the likely forgotten keel::Provides<T>.
    template <class C, class T>
    static constexpr Row derived{(!std::is_same_v<C, Cs> && !isProvider<T, Cs> && implements<Cs, T>())...};
    template <class C, class T>
    using Derived = Keep<derived<C, T>>;

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
            if constexpr (reportsBroken<C, T>) {
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

    using Cycle = Keep<onCycle>;
    using Sorted = std::conditional_t<
        acyclic, typename Reorder<TypeList<Cs...>, order.index, std::make_index_sequence<size>>::type, TypeList<Cs...>>;
};

}  // namespace detail

}  // namespace keel
