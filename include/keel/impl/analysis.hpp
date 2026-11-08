// keel/impl/analysis.hpp: the checks of the compose<...> list itself, and the analysis that
// keel::compose<...> and keel::isValid<...> share.
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
#include "diagnostics.hpp"
#include "graph.hpp"

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace keel {

namespace detail {

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

}  // namespace keel
