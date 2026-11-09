// The analysis keel::compose<...> and keel::isValid<...> share (Analysis).
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include "markers.hpp"
#include "type_list.hpp"
#include "type_name.hpp"
#include "lists.hpp"
#include "component_info.hpp"
#include "diagnostics.hpp"
#include "composition.hpp"

#include <array>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace keel {

namespace detail {

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

    // A type listed twice is reported once, at its second entry. The traits after
    // NotAComponent need a complete class.
    template <class C, std::size_t I>
    static auto check() {
        if constexpr (earlier<I> == 1) {
            return Outcome<DuplicateComponent<C>>{};
        } else if constexpr (earlier<I> > 1) {
            return Outcome<Passed<C, std::integral_constant<std::size_t, I>>>{};
        } else if constexpr (!derivesFromComponent<C>) {
            return Outcome<NotAComponent<C>>{};
        } else if constexpr (hiddenBase<Conditional, C>) {
            return Outcome<InaccessibleConditional<C>>{};
        } else if constexpr (hiddenBase<AsyncInit, C> || hiddenBase<AsyncStart, C>) {
            return Outcome<InaccessibleAsync<C>>{};
        } else if constexpr (std::is_abstract_v<C>) {
            return Outcome<AbstractComponent<C>>{};
        } else {
            return Outcome<Passed<C, std::integral_constant<std::size_t, I>>>{};
        }
    }
    template <class C, std::size_t I>
    using Check = typename decltype(check<C, I>())::type;
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

}  // namespace detail

}  // namespace keel
