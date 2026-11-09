// What an App reports about itself: keel::Phase, keel::Timing, toString(Phase), typeName<T>().
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include "type_name.hpp"

#include <chrono>
#include <string_view>

namespace keel {

/// A lifecycle hook of a component, as recorded in keel::Timing.
enum class Phase { init, start, stop, deinit };

/// The name of a phase: "init", "start", "stop" or "deinit".
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

/// How long one lifecycle call of one component took; an entry of App::timings().
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

/// The name keel uses for T, as the compiler spells it.
template <class T>
constexpr std::string_view typeName() noexcept {
    return detail::typeName<T>();
}

}  // namespace keel
