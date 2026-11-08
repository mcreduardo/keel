// keel/impl/storage.hpp: the storage of a composition: its components, constructed in
// dependency order from compose()'s arguments (or by default), destroyed in reverse.
//
// Part of keel.hpp: tools/amalgamate.py joins the parts into the single header.
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include "type_list.hpp"

#include <tuple>
#include <type_traits>

namespace keel {

namespace detail {

// ===========================================================================
// Storage of a composition

template <class C, class... Args>
C construct(Args&... args) {
    if constexpr (std::is_constructible_v<C, Args&...>) {
        return C(args...);
    } else if constexpr (std::is_default_constructible_v<C>) {
        return C();
    } else {
        static_assert(alwaysFalse<C>,
                      "keel: a component must be constructible from compose()'s arguments or default-constructible");
    }
}

// compose()'s arguments, passed to every Slot as one object: GCC 11 rejects expanding the
// arguments inside the expansion over the slots (Slot<Sorted>(args...)...).
template <class... Args>
struct Arguments {
    std::tuple<Args&...> refs;
};

template <class C>
struct Slot {
    template <class... Args>
    explicit Slot(const Arguments<Args...>& arguments)
        : component(std::apply([](Args&... args) { return construct<C>(args...); }, arguments.refs)) {}
    C component;
};

template <class List>
struct Storage;
template <class... Sorted>
struct Storage<TypeList<Sorted...>> : Slot<Sorted>... {
    // Bases are constructed in dependency order and destroyed in reverse.
    template <class... Args>
    explicit Storage(Args&... args) : Storage(Arguments<Args...>{{args...}}) {}
    template <class... Args>
    explicit Storage(const Arguments<Args...>& arguments) : Slot<Sorted>(arguments)... {}

    template <class T>
    T& get() noexcept {
        return static_cast<Slot<T>&>(*this).component;
    }
};

}  // namespace detail

}  // namespace keel
