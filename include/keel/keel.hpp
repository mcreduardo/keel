// keel/keel.hpp: application-level components with declared dependencies.
//
// C++20, no RTTI. The development entry point: it includes the parts in impl/, in order.
// tools/amalgamate.py joins them into the single header keel.hpp at the repository root.
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include "impl/markers.hpp"
#include "impl/type_list.hpp"
#include "impl/split.hpp"
#include "impl/type_name.hpp"
#include "impl/component_base.hpp"
#include "impl/registry.hpp"
#include "impl/diagnostics.hpp"
#include "impl/graph.hpp"
#include "impl/analysis.hpp"
#include "impl/component.hpp"
#include "impl/app.hpp"
#include "impl/lifecycle.hpp"
