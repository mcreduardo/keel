// Type names and type ids without RTTI, and concat(), which joins names into messages.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

namespace keel {

namespace detail {

/// Human-readable type name at compile time, without RTTI.
template <class T>
constexpr std::string_view typeName() {
#if defined(__clang__)
    // "... typeName() [T = Logger]": ends at the last ']' (the name may contain ']' and ';').
    constexpr std::string_view f = __PRETTY_FUNCTION__;
    constexpr auto begin = f.find("T = ") + 4;
    return f.substr(begin, f.rfind(']') - begin);
#elif defined(__GNUC__)
    // "... typeName() [with T = Logger; std::string_view = ...]": ends where the typedef is listed,
    // searched from the end (the name may contain ';' and ']'), else at the last ']'.
    constexpr std::string_view f = __PRETTY_FUNCTION__;
    constexpr auto begin = f.find("T = ") + 4;
    constexpr auto typedefs = f.rfind("; std::string_view = ");
    constexpr auto end = typedefs != std::string_view::npos && typedefs > begin ? typedefs : f.rfind(']');
    return f.substr(begin, end - begin);
#elif defined(_MSC_VER)
    // "... __cdecl keel::detail::typeName<class Logger>(void)"
    constexpr std::string_view f = __FUNCSIG__;
    constexpr auto begin = f.find("typeName<") + 9;
    constexpr auto end = f.rfind(">(void)");
    std::string_view name = f.substr(begin, end - begin);
    for (std::string_view prefix : {"class ", "struct ", "enum "}) {
        if (name.starts_with(prefix)) return name.substr(prefix.size());
    }
    return name;
#else
    return "<unknown type>";
#endif
}

using TypeId = const void*;

// Not const: the linker may merge identical read-only constants (MSVC /OPT:ICF), giving two
// types the same id.
template <class T>
struct TypeIdTag {
    static inline char tag{};
};

/// A unique id per type without RTTI: the address of a per-type variable.
template <class T>
constexpr TypeId typeId() noexcept {
    return &TypeIdTag<T>::tag;
}

inline std::string concat(std::initializer_list<std::string_view> parts) {
    std::string out;
    for (std::string_view p : parts) out += p;
    return out;
}

}  // namespace detail

}  // namespace keel
