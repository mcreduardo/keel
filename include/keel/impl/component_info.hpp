// keel/impl/component_info.hpp: what keel reads from a component type: its
// keel::Component<...> base and list (ComponentInfo), the entries init() checks (Needs), and
// traits: isConditional, hasAsyncInit, hasAsyncStart, implements, derivesFromComponent.
//
// Part of keel.hpp: tools/amalgamate.py joins the parts into the single header.
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include "markers.hpp"
#include "type_list.hpp"
#include "type_name.hpp"
#include "lists.hpp"
#include "component.hpp"

#include <array>
#include <string_view>
#include <type_traits>

namespace keel {

namespace detail {

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

// A C can be used as an I: I could be a component (couldBeComponent) and is C itself or a
// public, unambiguous base of C. A pointer conversion checks access and ambiguity, and is
// simply false for an incomplete I (C can't derive from it then).
template <class C, class I>
constexpr bool implements() {
    if constexpr (couldBeComponent<I>) {
        return std::is_convertible_v<C*, I*>;
    } else {
        return false;
    }
}

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

}  // namespace detail

}  // namespace keel
