// Reading the lists a user writes (Split, ExpandBundles), and keel::Provides and keel::Alternative.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include "markers.hpp"
#include "type_list.hpp"

#include <type_traits>

namespace keel {

namespace detail {

// How an entry uses the type it names. The listed* and misnested kinds are mistakes,
// reported at compose time.
enum class Use {
    required,
    optional,
    ordering,
    listedProvides,
    listedAlternative,
    listedAsync,
    all,
    optionalAll,
    misnested
};

struct EmptySplit {
    using required = TypeList<>;
    using optional = TypeList<>;
    using after = TypeList<>;
    using all = TypeList<>;
    using misnested = TypeList<>;
    using listedProvides = TypeList<>;
    using listedAlternatives = TypeList<>;
    using listedAsync = TypeList<>;
};

// Separates the entries of a keel::Component<...> list by kind. `target` is the type the entry names.
template <class D>
struct Split : EmptySplit {
    using required = TypeList<D>;
    using target = D;
    static constexpr Use use = Use::required;
};
template <class T>
struct Split<Optional<T>> : EmptySplit {
    using optional = TypeList<T>;
    using target = T;
    static constexpr Use use = Use::optional;
};
template <class T>
struct Split<After<T>> : EmptySplit {
    using after = TypeList<T>;
    using target = T;
    static constexpr Use use = Use::ordering;
};
template <class I>
struct Split<Provides<I>> : EmptySplit {
    using listedProvides = TypeList<I>;
    using target = I;
    static constexpr Use use = Use::listedProvides;
};
template <class I>
struct Split<Alternative<I>> : EmptySplit {
    using listedAlternatives = TypeList<I>;
    using target = I;
    static constexpr Use use = Use::listedAlternative;
};
template <class I>
struct Split<All<I>> : EmptySplit {
    using all = TypeList<I>;
    using target = I;
    static constexpr Use use = Use::all;
};
template <class I>
struct Split<Optional<All<I>>> : EmptySplit {
    using all = TypeList<I>;
    using target = I;
    static constexpr Use use = Use::optionalAll;
};
// Accessors accept I for it, so MisnestedAll is the one error.
template <class I>
struct Split<All<Optional<I>>> : EmptySplit {
    using misnested = TypeList<I>;
    using target = I;
    static constexpr Use use = Use::misnested;
};

template <>
struct Split<AsyncInit> : EmptySplit {
    using listedAsync = TypeList<AsyncInit>;
    using target = AsyncInit;
    static constexpr Use use = Use::listedAsync;
};
template <>
struct Split<AsyncStart> : EmptySplit {
    using listedAsync = TypeList<AsyncStart>;
    using target = AsyncStart;
    static constexpr Use use = Use::listedAsync;
};

template <class T>
inline constexpr bool isAsyncMarker = std::is_same_v<T, AsyncInit> || std::is_same_v<T, AsyncStart>;

template <class T>
inline constexpr bool isDependencyMarker = isAsyncMarker<T>;
template <class T>
inline constexpr bool isDependencyMarker<Optional<T>> = true;
template <class T>
inline constexpr bool isDependencyMarker<After<T>> = true;
template <class T>
inline constexpr bool isDependencyMarker<Provides<T>> = true;
template <class T>
inline constexpr bool isDependencyMarker<Alternative<T>> = true;
template <class T>
inline constexpr bool isDependencyMarker<All<T>> = true;

// A type an entry may name: a class, not cv-qualified, not a marker. Anything else would never match.
template <class T>
inline constexpr bool couldBeComponent =
    std::is_class_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T> && !isDependencyMarker<T>;

template <class C>
inline constexpr bool isBundle = false;
template <class... Cs>
inline constexpr bool isBundle<Bundle<Cs...>> = true;

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
    using type = Expanded<typename Expand<TypeList<>, TypeList<>, TypeList<>, Cs...>::components,
                          typename Expand<TypeList<>, TypeList<>, TypeList<>, Cs...>::repeated>;
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
