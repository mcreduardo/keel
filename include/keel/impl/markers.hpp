// The vocabulary for declaring components: Error, markers, Bundle, base classes. See docs/dependencies.md.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include <stdexcept>

namespace keel {

/// Composition and usage errors reported at runtime.
class Error : public std::logic_error {
public:
    using std::logic_error::logic_error;
};

/// Optional dependency: findComponent<T>() returns it, or nullptr if absent or disabled.
template <class T>
struct Optional {};

/// Ordering-only dependency: init after T if composed; After<All<I>>: after every provider of I. No access.
template <class T>
struct After {};

/// Base class: this component implements the interface I (derive from I publicly too).
template <class I>
struct Provides;  // defined in lists.hpp because of physical design limitations

/// Base class: implements I as one of several implementations chosen at runtime.
template <class I>
struct Alternative;  // defined in lists.hpp because of physical design limitations

/// Dependency on every provider of the interface I, in init order.
template <class I>
struct All {};

/// Base class that runs this component's init() (and enabled()) on a thread of its own.
struct AsyncInit {};

/// Base class that runs this component's start() on a thread of its own.
struct AsyncStart {};

/// A named group of components, listed in compose<...> or App<...> in place of them.
template <class... Components>
struct Bundle {};

// Forward-declared for Conditional's friend declaration.
namespace detail {
class Registry;
}

/// Base class of a component that configuration may turn off by overriding enabled().
class Conditional {
protected:
    virtual bool enabled() const = 0;

    Conditional() = default;
    Conditional(const Conditional&) = default;
    Conditional& operator=(const Conditional&) = default;
    virtual ~Conditional() = default;

private:
    friend class detail::Registry;
};

}  // namespace keel
