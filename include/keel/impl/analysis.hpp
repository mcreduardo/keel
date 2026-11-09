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
            return Outcome<Passed>{};
        } else if constexpr (!derivesFromComponent<C>) {
            return Outcome<NotAComponent<C>>{};
        } else if constexpr (hiddenBase<Conditional, C>) {
            return Outcome<InaccessibleConditional<C>>{};
        } else if constexpr (hiddenBase<AsyncInit, C> || hiddenBase<AsyncStart, C>) {
            return Outcome<InaccessibleAsync<C>>{};
        } else if constexpr (std::is_abstract_v<C>) {
            return Outcome<AbstractComponent<C>>{};
        } else {
            return Outcome<Passed>{};
        }
    }
    template <class C, std::size_t I>
    using Check = typename decltype(check<C, I>())::type;
};

// The outcomes, each Passed or a failure, in the order they are reported.
template <class List, class Indices>
struct ListOutcomes;
template <class... Cs, std::size_t... Is>
struct ListOutcomes<ListInfo<Cs...>, std::index_sequence<Is...>> {
    using type = TypeList<typename ListInfo<Cs...>::template Check<Cs, Is>...>;
};

// Checked per component, whatever refers to its interfaces.
template <class C>
using ConditionalCheck =
    std::conditional_t<std::is_base_of_v<AlternativeMarker, C> && !isConditional<C>, AlternativeNotConditional<C>,
                       Passed>;

// A list with a duplicate is not checked further: the duplicate is the one error.
template <bool uniqueEntries, class Info, class C, class Entries = EntriesOf<C>>
struct ComponentOutcomes {
    using type = TypeList<ComponentListCheck<ComponentOf<C>>>;
};
template <class Info, class C, class... Entries>
struct ComponentOutcomes<true, Info, C, TypeList<Entries...>> {
    using type = TypeList<ConditionalCheck<C>, typename Info::template Check<C, Entries>...>;
};
template <class Info, class List>
struct ComponentsOutcomes;
template <class Info, class... Cs>
struct ComponentsOutcomes<Info, TypeList<Cs...>> {
    using type = Concat<TypeList<>, typename ComponentOutcomes<uniqueEntriesOf<Cs>, Info, Cs>::type...>;
};

// The graph analysis runs only on a list of distinct, concrete components, so a list that
// isn't one reports that and nothing else (naming the components' checks reads their lists).
template <class Expansion>
struct Analysis;
template <class... Cs, class... Repeated>
struct Analysis<Expanded<TypeList<Cs...>, TypeList<Repeated...>>> {
    using List = ListInfo<Cs...>;
    using OfList = typename ListOutcomes<List, std::index_sequence_for<Cs...>>::type;
    static constexpr bool wellFormed = allPassed<OfList>;
    using Components = std::conditional_t<wellFormed, TypeList<Cs...>, TypeList<>>;
    using Info = Composition<Components>;

    // compose<...> reports them (Report), keel::isValid<...> returns ok.
    using Outcomes = Concat<TypeList<DuplicateBundle<Repeated>...>, OfList,
                            typename ComponentsOutcomes<Info, Components>::type,
                            TypeList<std::conditional_t<(sizeof...(Cs) > 0), Passed, EmptyComposition<>>,
                                     std::conditional_t<Info::acyclic, Passed, DependencyCycle<typename Info::Cycle>>>>;
    static constexpr bool ok = allPassed<Outcomes>;
};

}  // namespace detail

}  // namespace keel
