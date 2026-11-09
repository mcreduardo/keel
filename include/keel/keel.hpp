// keel/keel.hpp: application-level components with declared dependencies. C++20, no RTTI.
// The development entry point: includes the parts in impl/ in order of definition;
// tools/amalgamate.py joins them into the single header keel.hpp.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

// Public: the vocabulary for declaring components, and what an App reports about itself.
#include "impl/markers.hpp"
#include "impl/introspection.hpp"

// Detail: type lists, type names and ids, reading the user's lists.
#include "impl/type_list.hpp"
#include "impl/type_name.hpp"
#include "impl/lists.hpp"

// Public: keel::Component<...>, the base class of every component.
#include "impl/component.hpp"

// Detail: the compile-time analysis of a composition.
#include "impl/component_info.hpp"
#include "impl/diagnostics.hpp"
#include "impl/graph.hpp"
#include "impl/composition.hpp"
#include "impl/analysis.hpp"

// Detail: the runtime: lookups, storage, lifecycle phases.
#include "impl/registry.hpp"
#include "impl/storage.hpp"
#include "impl/lifecycle.hpp"

// Public: keel::App, compose, isValid, requireValid and run.
#include "impl/app.hpp"
