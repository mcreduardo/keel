// The dependency graph at compile time: the init order and the components on cycles.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include <array>
#include <cstddef>

namespace keel {

namespace detail {

// graph[i][j]: component i depends on component j.
template <std::size_t N>
using Graph = std::array<std::array<bool, N>, N>;

template <std::size_t N>
struct TopologicalOrder {
    std::array<std::size_t, N> index{};  // index[k]: the component placed k-th
    std::array<bool, N> placed{};
    std::size_t count = 0;  // < N if some components wait on each other (a cycle)
};

// Kahn's algorithm, always placing the earliest ready component, so independent components
// keep the compose<...> order. O(N²).
template <std::size_t N>
constexpr TopologicalOrder<N> topologicalOrder(const Graph<N>& graph) {
    TopologicalOrder<N> o;
    std::array<std::size_t, N> waitingFor{};  // dependencies not placed yet
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = 0; j < N; ++j) waitingFor[i] += graph[i][j] ? 1 : 0;
    }
    while (o.count < N) {
        std::size_t next = N;
        for (std::size_t i = 0; i < N && next == N; ++i) {
            if (!o.placed[i] && waitingFor[i] == 0) next = i;
        }
        if (next == N) break;  // the rest wait on each other
        o.placed[next] = true;
        o.index[o.count++] = next;
        for (std::size_t i = 0; i < N; ++i) {
            if (graph[i][next]) --waitingFor[i];
        }
    }
    return o;
}

// Iterative depth-first searches, forward or backward; each row is scanned once overall: O(N²).
template <std::size_t N>
struct CycleSearch {
    constexpr CycleSearch(const Graph<N>& g, const std::array<bool, N>& p, bool f) : graph(g), placed(p), forward(f) {}

    const Graph<N>& graph;
    const std::array<bool, N>& placed;
    bool forward;
    std::array<std::size_t, N> mark{};  // the search that reached it, from 1; 0: none yet
    std::array<std::size_t, N> finished{};
    std::size_t finishedCount = 0;
    std::size_t searches = 0;

    constexpr bool edge(std::size_t from, std::size_t to) const {
        return forward ? graph[from][to] : graph[to][from];
    }
    constexpr void search(std::size_t root) {
        std::array<std::size_t, N> next{};  // the next column to scan in each row
        std::array<std::size_t, N> stack{};
        std::size_t depth = 0;
        stack[depth++] = root;
        mark[root] = ++searches;
        while (depth > 0) {
            const std::size_t v = stack[depth - 1];
            std::size_t& j = next[v];
            while (j < N && (placed[j] || mark[j] != 0 || !edge(v, j))) ++j;
            if (j < N) {
                mark[j] = searches;
                stack[depth++] = j;
            } else {
                finished[finishedCount++] = v;
                --depth;
            }
        }
    }
};

// Kosaraju's algorithm, O(N²): unplaced components in an SCC of two or more, or self-dependent.
// Components that only depend on a cycle are left out. Only evaluated when there is a cycle.
template <std::size_t N>
constexpr std::array<bool, N> componentsOnCycles(const Graph<N>& graph, const std::array<bool, N>& placed) {
    CycleSearch<N> first(graph, placed, true);
    for (std::size_t i = 0; i < N; ++i) {
        if (!placed[i] && first.mark[i] == 0) first.search(i);
    }
    // Backward searches, latest finished first; each reaches exactly one SCC.
    CycleSearch<N> second(graph, placed, false);
    for (std::size_t k = first.finishedCount; k-- > 0;) {
        if (second.mark[first.finished[k]] == 0) second.search(first.finished[k]);
    }
    std::array<std::size_t, N + 1> size{};
    for (std::size_t i = 0; i < N; ++i) ++size[second.mark[i]];  // mark 0: placed
    std::array<bool, N> out{};
    for (std::size_t i = 0; i < N; ++i) out[i] = !placed[i] && (size[second.mark[i]] > 1 || graph[i][i]);
    return out;
}

}  // namespace detail

}  // namespace keel
