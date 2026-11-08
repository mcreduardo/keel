// keel/impl/type_list.hpp: type lists, and the expansion of keel::Bundle<...>s.
//
// Part of keel.hpp: tools/amalgamate.py joins the parts into the single header.
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include "markers.hpp"

#include <type_traits>

namespace keel {

namespace detail {

// ===========================================================================
// Type utilities

template <class... Ts>
struct TypeList {};

template <class T, class List>
struct ContainsImpl;
template <class T, class... Ts>
struct ContainsImpl<T, TypeList<Ts...>> : std::bool_constant<(std::is_same_v<T, Ts> || ...)> {};
template <class T, class List>
inline constexpr bool contains = ContainsImpl<T, List>::value;

template <class... Ts>
struct UniqueImpl : std::true_type {};
template <class T, class... Ts>
struct UniqueImpl<T, Ts...> : std::bool_constant<!(std::is_same_v<T, Ts> || ...) && UniqueImpl<Ts...>::value> {};
template <class List>
struct UniqueList;
template <class... Ts>
struct UniqueList<TypeList<Ts...>> : UniqueImpl<Ts...> {};

template <class... Lists>
struct ConcatImpl {
    using type = TypeList<>;
};
template <class... As>
struct ConcatImpl<TypeList<As...>> {
    using type = TypeList<As...>;
};
template <class... As, class... Bs, class... Rest>
struct ConcatImpl<TypeList<As...>, TypeList<Bs...>, Rest...> : ConcatImpl<TypeList<As..., Bs...>, Rest...> {};
template <class... Lists>
using Concat = typename ConcatImpl<Lists...>::type;

template <class T>
inline constexpr bool alwaysFalse = false;

template <class C>
inline constexpr bool isBundle = false;
template <class... Cs>
inline constexpr bool isBundle<Bundle<Cs...>> = true;

template <class List, class T>
struct AppendImpl;
template <class... Ts, class T>
struct AppendImpl<TypeList<Ts...>, T> {
    using type = TypeList<Ts..., T>;
};
template <class List, class T>
using Append = typename AppendImpl<List, T>::type;

// The result of expanding the keel::Bundle<...>s of a compose<...> list: the components,
// and the bundles reached more than once in the tree (each once). A repeated bundle is
// expanded only the first time, so it is reported as one DuplicateBundle<B>, not as a
// duplicate of each of its components.
template <class Components, class RepeatedBundles = TypeList<>>
struct Expanded {
    using components = Components;
    using repeated = RepeatedBundles;
};

// Expands the list left to right, in place: a bundle's components take its position.
// Seen: the bundles expanded so far.
template <class Seen, class Repeated, class Out, class... Rest>
struct Expand : Expanded<Out, Repeated> {};
template <bool bundle, class Seen, class Repeated, class Out, class First, class... Rest>
struct ExpandFirst : Expand<Seen, Repeated, Append<Out, First>, Rest...> {};
template <class Seen, class Repeated, class Out, class First, class... Rest>
struct Expand<Seen, Repeated, Out, First, Rest...>
    : ExpandFirst<isBundle<First>, Seen, Repeated, Out, First, Rest...> {};
template <class Seen, class Repeated, class Out, class... Inner, class... Rest>
struct ExpandFirst<true, Seen, Repeated, Out, Bundle<Inner...>, Rest...>
    : std::conditional_t<
          contains<Bundle<Inner...>, Seen>,
          Expand<Seen,
                 std::conditional_t<contains<Bundle<Inner...>, Repeated>, Repeated, Append<Repeated, Bundle<Inner...>>>,
                 Out, Rest...>,
          Expand<Append<Seen, Bundle<Inner...>>, Repeated, Out, Inner..., Rest...>> {};

// A list without bundles is used as is.
template <bool anyBundle, class... Cs>
struct ExpandImpl {
    using type = Expanded<TypeList<Cs...>>;
};
template <class... Cs>
struct ExpandImpl<true, Cs...> {
    using type = Expanded<typename Expand<TypeList<>, TypeList<>, TypeList<>, Cs...>::components,
                          typename Expand<TypeList<>, TypeList<>, TypeList<>, Cs...>::repeated>;
};
template <class... Cs>
using ExpandBundles = typename ExpandImpl<(isBundle<Cs> || ...), Cs...>::type;

// The first type of a TypeList, or Fallback if it is empty.
template <class List, class Fallback>
struct FirstOrImpl {
    using type = Fallback;
};
template <class T, class... Ts, class Fallback>
struct FirstOrImpl<TypeList<T, Ts...>, Fallback> {
    using type = T;
};
template <class List, class Fallback>
using FirstOr = typename FirstOrImpl<List, Fallback>::type;

// The class of a pointer to member function: `R C::*` also matches `R (C::*)(Args...)`,
// with R deduced as the function type `R(Args...)`.
template <class M>
struct MemberOf;
template <class R, class C>
struct MemberOf<R C::*> {
    using type = C;
};

}  // namespace detail

}  // namespace keel
