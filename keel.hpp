// keel.hpp: application-level components with declared dependencies.
//
// Single header, C++20, no RTTI.
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include <stdexcept>

namespace keel {

/// Composition and usage errors.
class Error : public std::logic_error {
public:
    using std::logic_error::logic_error;
};

}  // namespace keel
