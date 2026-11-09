// What keel reads from a component type: its keel::Component<...> list, Needs, and traits.
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

// Deduced, so that members of C named like Component's own (Entries, ...) don't hide them.
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
// Provides<I>/Alternative<I> listed by mistake count as provided, so the mistake is the one error.
template <class C>
using ListedOf = typename ComponentInfo<C>::Listed;
template <class C>
using ListedAlternativesOf = typename ComponentInfo<C>::Alternatives;
template <class C>
inline constexpr bool uniqueEntriesOf = ComponentOf<C>::uniqueEntries;

// An entry init() checks: a required or Optional<T> dependency, which may resolve to alternatives.
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
template <class C>
using Needs = NeedsTable<typename NeedsImpl<EntriesOf<C>>::Checked>;

// Public and once (else InaccessibleConditional at compose time). False for an incomplete C.
template <class C>
inline constexpr bool isConditional = std::is_convertible_v<C*, Conditional*>;

// Public and once (else InaccessibleAsync at compose time).
template <class C>
inline constexpr bool hasAsyncInit = std::is_convertible_v<C*, AsyncInit*>;
template <class C>
inline constexpr bool hasAsyncStart = std::is_convertible_v<C*, AsyncStart*>;

// The pointer conversion checks access and ambiguity, and is false for an incomplete I.
template <class C, class I>
constexpr bool implements() {
    if constexpr (couldBeComponent<I>) {
        return std::is_convertible_v<C*, I*>;
    } else {
        return false;
    }
}

// The pointer conversion checks access and ambiguity. An incomplete T is not a component.
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
