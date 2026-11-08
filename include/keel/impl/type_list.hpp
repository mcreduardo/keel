// keel/impl/type_list.hpp: type lists (TypeList, contains, UniqueList, Concat, Append,
// FirstOr, KeepIf, Reorder) and other type-level helpers (alwaysFalse, MemberOf,
// hasPlainCallOperator).
//
// Part of keel.hpp: tools/amalgamate.py joins the parts into the single header.
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

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

template <class List, class T>
struct AppendImpl;
template <class... Ts, class T>
struct AppendImpl<TypeList<Ts...>, T> {
    using type = TypeList<Ts..., T>;
};
template <class List, class T>
using Append = typename AppendImpl<List, T>::type;

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

// The types of a list whose entry in Keep is true, and the types of a list in the order of
// Index. Class templates at namespace scope rather than member functions of Composition:
// MSVC doesn't expand a pack inside a member function's return type there.
template <class List, auto Keep, class Sequence>
struct KeepIf;
template <class... Ts, auto Keep, std::size_t... Is>
struct KeepIf<TypeList<Ts...>, Keep, std::index_sequence<Is...>> {
    using type = Concat<std::conditional_t<Keep[Is], TypeList<Ts>, TypeList<>>...>;
};

template <class List, auto Index, class Sequence>
struct Reorder;
template <class... Ts, auto Index, std::size_t... Is>
struct Reorder<TypeList<Ts...>, Index, std::index_sequence<Is...>> {
    using type = TypeList<std::tuple_element_t<Index[Is], std::tuple<Ts...>>...>;
};

// A class whose operator() is a single non-template function (not a generic lambda).
template <class F>
inline constexpr bool hasPlainCallOperator = requires { &F::operator(); };

}  // namespace detail

}  // namespace keel
