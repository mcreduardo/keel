// Reading the lists a user writes (Split, ExpandBundles), and keel::Provides and keel::Alternative.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include "markers.hpp"
#include "type_list.hpp"

#include <type_traits>

namespace keel {

namespace detail {

// How an entry uses the type it names.
enum class Use { required, optional, ordering, all, optionalAll, orderingAll };

// What an entry of a keel::Component<...> list names (`target`), and how (`use`).
template <class D, Use U = Use::required>
struct Split {
    using target = D;
    static constexpr Use use = U;
};
template <class T>
struct Split<Optional<T>> : Split<T, Use::optional> {};
template <class T>
struct Split<After<T>> : Split<T, Use::ordering> {};
template <class I>
struct Split<All<I>> : Split<I, Use::all> {};
template <class I>
struct Split<Optional<All<I>>> : Split<I, Use::optionalAll> {};
template <class I>
struct Split<After<All<I>>> : Split<I, Use::orderingAll> {};

template <class Entry, Use... uses>
inline constexpr bool usedAs = ((Split<Entry>::use == uses) || ...);
// The targets of the entries whose use is one of uses, in list order.
template <class Entries, Use... uses>
struct TargetsOfImpl;
template <class... Entries, Use... uses>
struct TargetsOfImpl<TypeList<Entries...>, uses...> {
    using type = Concat<
        std::conditional_t<usedAs<Entries, uses...>, TypeList<typename Split<Entries>::target>, TypeList<>>...>;
};
template <class Entries, Use... uses>
using TargetsOf = typename TargetsOfImpl<Entries, uses...>::type;

// Optional, After, All, and the markers that are base classes.
template <class T>
inline constexpr bool isMarker = Split<T>::use != Use::required;
template <class I>
inline constexpr bool isMarker<Provides<I>> = true;
template <class I>
inline constexpr bool isMarker<Alternative<I>> = true;
template <>
inline constexpr bool isMarker<AsyncInit> = true;
template <>
inline constexpr bool isMarker<AsyncStart> = true;

// A type an entry may name: a class, not cv-qualified, not a marker. Anything else would never match.
template <class T>
inline constexpr bool couldBeComponent =
    std::is_class_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T> && !isMarker<T>;

template <class C>
inline constexpr bool isBundle = false;
template <class... Cs>
inline constexpr bool isBundle<Bundle<Cs...>> = true;

// An entry that names a type a component can be or provide; any other is InvalidDependency.
template <class Entry>
inline constexpr bool validEntry =
    couldBeComponent<typename Split<Entry>::target> && !isBundle<typename Split<Entry>::target>;
template <class... Entries>
using ValidTargets =
    Concat<std::conditional_t<validEntry<Entries>, TypeList<typename Split<Entries>::target>, TypeList<>>...>;

// The class an entry is about, under its qualifiers and markers: keel::Optional<const T&> is T.
template <class T>
struct StrippedImpl {
    using type = T;
};
template <class T>
struct StrippedImpl<Optional<T>> : StrippedImpl<std::remove_cvref_t<T>> {};
template <class T>
struct StrippedImpl<After<T>> : StrippedImpl<std::remove_cvref_t<T>> {};
template <class T>
struct StrippedImpl<All<T>> : StrippedImpl<std::remove_cvref_t<T>> {};
template <class T>
struct StrippedImpl<Provides<T>> : StrippedImpl<std::remove_cvref_t<T>> {};
template <class T>
struct StrippedImpl<Alternative<T>> : StrippedImpl<std::remove_cvref_t<T>> {};
template <class T>
using Stripped = typename StrippedImpl<std::remove_cvref_t<T>>::type;

// T is what an invalid or duplicated entry is about: its accessors leave that one error alone.
template <class T, class... Entries>
inline constexpr bool listMistakeOn =
    ((!validEntry<Entries> && std::is_same_v<Stripped<typename Split<Entries>::target>, Stripped<T>>) || ...) ||
    (0 + ... + (validEntry<Entries> && std::is_same_v<typename Split<Entries>::target, T> ? 1 : 0)) > 1;

// Of several invalid entries about one type, only the first is reported.
template <class T, class Entries>
struct FirstInvalidOnImpl;
template <class T, class... Entries>
struct FirstInvalidOnImpl<T, TypeList<Entries...>> {
    using type = FirstOr<Concat<std::conditional_t<!validEntry<Entries> &&
                                                       std::is_same_v<Stripped<typename Split<Entries>::target>, T>,
                                                   TypeList<Entries>, TypeList<>>...>,
                         void>;
};
template <class T, class Entries>
using FirstInvalidOn = typename FirstInvalidOnImpl<T, Entries>::type;

// A repeated bundle is expanded only the first time, so it is one DuplicateBundle<B>, not a
// duplicate of each of its components.
template <class Components, class RepeatedBundles = TypeList<>>
struct Expanded {
    using components = Components;
    using repeated = RepeatedBundles;
};

// Left to right, in place: a bundle's components take its position.
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

template <bool anyBundle, class... Cs>
struct ExpandImpl {
    using type = Expanded<TypeList<Cs...>>;
};
template <class... Cs>
struct ExpandImpl<true, Cs...> {
    using type = typename Expand<TypeList<>, TypeList<>, TypeList<>, Cs...>::Expanded;  // the base class
};
template <class... Cs>
using ExpandBundles = typename ExpandImpl<(isBundle<Cs> || ...), Cs...>::type;

}  // namespace detail

// Defined here, after couldBeComponent. I is checked in the constructor, not the class body:
// a failed check there would make every later use of the component an error.
template <class I>
struct Provides {
    constexpr Provides() noexcept {
        static_assert(detail::couldBeComponent<I>,
                      "keel: keel::Provides<I> needs an interface I that a dependency could name: a class type, "
                      "not const or volatile, not a reference, and not a keel marker (keel::Optional, keel::All, "
                      "...). Write keel::Provides<Storage>, not keel::Provides<const Storage>.");
    }
};

namespace detail {
// Lets keel know a component is an alternative of something without knowing I.
struct AlternativeMarker {};
}  // namespace detail

// Defined here, after couldBeComponent; checked as keel::Provides<I> is.
template <class I>
struct Alternative : detail::AlternativeMarker {
    constexpr Alternative() noexcept {
        static_assert(detail::couldBeComponent<I>,
                      "keel: keel::Alternative<I> needs an interface I that a dependency could name: a class type, "
                      "not const or volatile, not a reference, and not a keel marker (keel::Optional, keel::All, "
                      "...). Write keel::Alternative<Cache>, not keel::Alternative<const Cache>.");
    }
};

}  // namespace keel
