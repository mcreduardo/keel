// keel/impl/introspection.hpp: what an App reports about itself: keel::Phase and
// keel::Timing (the entries of App::timings()), keel::toString(Phase), and keel::typeName<T>(),
// the names in App::initOrder(), timings() and toDot(). Those three are members of the App
// (app.hpp).
//
// Part of keel.hpp: tools/amalgamate.py joins the parts into the single header.
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include "type_name.hpp"

#include <chrono>
#include <string_view>

namespace keel {

/// A lifecycle hook of a component, as recorded in keel::Timing.
enum class Phase { init, start, stop, deinit };

/// "init", "start", "stop" or "deinit".
constexpr std::string_view toString(Phase phase) noexcept {
    switch (phase) {
        case Phase::init:
            return "init";
        case Phase::start:
            return "start";
        case Phase::stop:
            return "stop";
        case Phase::deinit:
            return "deinit";
    }
    return "?";
}

/// How long one lifecycle call of one component took (App::timings()).
struct Timing {
    std::string_view component;            // the component's name, as in App::initOrder()
    Phase phase = Phase::init;
    std::chrono::nanoseconds duration{};   // measured with std::chrono::steady_clock
    bool failed = false;                   // the call threw (init() or start()); the rollback follows
};

namespace detail {
// Defined in type_name.hpp, which the single header places after this part.
template <class T>
constexpr std::string_view typeName();
}  // namespace detail

/// The name keel uses for the type T: in App::initOrder(), keel::Timing, App::toDot(),
/// componentName() and error messages. The type as the compiler spells it
/// (`Logger`, `edge::Imu<edge::Front>`), which may differ between compilers.
template <class T>
constexpr std::string_view typeName() noexcept {
    return detail::typeName<T>();
}

}  // namespace keel
