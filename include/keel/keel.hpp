// keel/keel.hpp: application-level components with declared dependencies.
//
// C++20, no RTTI. The development entry point: it includes the parts in impl/, in order.
// tools/amalgamate.py joins them into the single header keel.hpp at the repository root.
//
// The order is the order of definition: a part uses only what the parts above it define.
// The public parts come where what they are built from is defined.
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

// Public: the vocabulary for declaring components (markers.hpp), and what an App reports
// about itself (introspection.hpp).
#include "impl/markers.hpp"
#include "impl/introspection.hpp"

// Detail: type lists, type names and ids, and reading the lists a user writes: what
// keel::Component<...> is built from.
#include "impl/type_list.hpp"
#include "impl/type_name.hpp"
#include "impl/lists.hpp"

// Public: keel::Component<...>, the base class of every component.
#include "impl/component.hpp"

// Detail: the compile-time analysis of a composition: what keel reads from each component,
// the diagnostics, the dependency graph, the composition, and the analysis that compose and
// isValid share.
#include "impl/component_info.hpp"
#include "impl/diagnostics.hpp"
#include "impl/graph.hpp"
#include "impl/composition.hpp"
#include "impl/analysis.hpp"

// Detail: the runtime: lookups, the storage of the components, the lifecycle phases.
#include "impl/registry.hpp"
#include "impl/storage.hpp"
#include "impl/lifecycle.hpp"

// Public: keel::App, compose, isValid, requireValid and run.
#include "impl/app.hpp"
