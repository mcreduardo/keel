// keel/impl/split.hpp: the kinds of entries of a keel::Component<...> list (Split),
// the definitions of keel::Provides and keel::Alternative, and traits of components.
//
// Part of keel.hpp: tools/amalgamate.py joins the parts into the single header.
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include "markers.hpp"
#include "type_list.hpp"

#include <type_traits>

namespace keel {

namespace detail {

// How a component uses the type an entry of its keel::Component<...> list names.
// `misnested` is All<Optional<I>>, and `listedProvides` / `listedAlternative`
// keel::Provides<I> / keel::Alternative<I>, which are base classes, not entries
// (ProvidesAsDependency, AlternativeAsDependency), and `listedAsync` keel::AsyncInit or
// keel::AsyncStart, base classes too (AsyncAsDependency); all are reported at compose time.
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

// Separates the entries of a keel::Component<...> list by kind: required, optional,
// ordering only, collections (All<I>, Optional<All<I>>), and the base classes listed by
// mistake (Provides<I>, Alternative<I>, AsyncInit, AsyncStart). `target` is the type the
// entry names (for AsyncInit and AsyncStart, the marker itself).
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

// Optional<T>, After<T> and Optional<All<T>> may name a type that isn't composed, and
// All<T> an interface, but it must be a type that could be one: a class,
// not const or volatile, not another marker (AsyncInit and AsyncStart included). Anything
// else would never match.
template <class T>
inline constexpr bool couldBeComponent =
    std::is_class_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T> && !isDependencyMarker<T>;

}  // namespace detail

// See the declaration above. Empty. I is checked in the constructor rather than in the class
// body: a failed check there would make the derived component invalid, and every later use
// of it would add an error.
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
// The common base of every keel::Alternative<I>: whether a component is an alternative of
// anything, without knowing I (AlternativeNotConditional).
struct AlternativeMarker {};
}  // namespace detail

// See the declaration above. Checked as keel::Provides<I> is.
template <class I>
struct Alternative : detail::AlternativeMarker {
    constexpr Alternative() noexcept {
        static_assert(detail::couldBeComponent<I>,
                      "keel: keel::Alternative<I> needs an interface I that a dependency could name: a class type, "
                      "not const or volatile, not a reference, and not a keel marker (keel::Optional, keel::All, "
                      "...). Write keel::Alternative<Cache>, not keel::Alternative<const Cache>.");
    }
};

namespace detail {

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

// A C can be used as an I: I could be a component (above) and is C itself or a public,
// unambiguous base of C. A pointer conversion checks access and ambiguity, and is simply
// false for an incomplete I (C can't derive from it then).
template <class C, class I>
constexpr bool implements() {
    if constexpr (couldBeComponent<I>) {
        return std::is_convertible_v<C*, I*>;
    } else {
        return false;
    }
}

}  // namespace detail

}  // namespace keel
