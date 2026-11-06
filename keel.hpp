// keel.hpp: application-level components with declared dependencies.
//
// Single header, C++20, no RTTI.
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace keel {

/// Composition and usage errors: a dependency that isn't composed, a dependency used
/// before it is initialized, and so on.
class Error : public std::logic_error {
public:
    using std::logic_error::logic_error;
};

template <class... Dependencies>
class Component;

template <class... Components>
class App;

namespace detail {

// ===========================================================================
// Type utilities

template <class... Ts>
struct TypeList {};

template <class T, class List>
struct ContainsImpl;
template <class T, class... Ts>
struct ContainsImpl<T, TypeList<Ts...>> : std::bool_constant<(std::is_same_v<T, Ts> || ...)> {};
template <class T, class List>
inline constexpr bool contains = ContainsImpl<T, List>::value;

template <class... Ts>
struct UniqueImpl : std::true_type {};
template <class T, class... Ts>
struct UniqueImpl<T, Ts...> : std::bool_constant<!(std::is_same_v<T, Ts> || ...) && UniqueImpl<Ts...>::value> {};

template <class T>
inline constexpr bool alwaysFalse = false;

/// Human-readable type name at compile time, without RTTI.
template <class T>
constexpr std::string_view typeName() {
#if defined(__clang__) || defined(__GNUC__)
    // clang: "std::string_view keel::detail::typeName() [T = Logger]"
    // gcc:   "constexpr std::string_view keel::detail::typeName() [with T = Logger; ...]"
    constexpr std::string_view f = __PRETTY_FUNCTION__;
    constexpr auto begin = f.find("T = ") + 4;
    constexpr auto end = f.find_first_of(";]", begin);
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

template <class T>
struct TypeIdTag {
    static constexpr char tag = 0;
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

class Registry;

// ===========================================================================
// Type-erased base of every component. Users derive from keel::Component<...>.

class ComponentBase {
public:
    ComponentBase() = default;
    ComponentBase(const ComponentBase&) = delete;
    ComponentBase& operator=(const ComponentBase&) = delete;
    virtual ~ComponentBase() = default;

    /// The component's type name, available once it is part of an application.
    std::string_view componentName() const noexcept { return name_; }

protected:
    /// Acquire and configure. Throw to abort startup.
    virtual void init() {}
    /// Release. Runs in reverse order.
    virtual void deinit() {}

private:
    friend class Registry;
    template <class...>
    friend class keel::Component;

    enum class State : unsigned char { created, initialized, deinitialized };

    void* lookup(TypeId id, std::string_view dependency) const;

    Registry* registry_ = nullptr;
    std::string_view name_;
    State state_ = State::created;
};

// ===========================================================================
// Runtime state of an App: the components in init order, and lookups by type.

struct Binding {
    ComponentBase* owner;
    void* object;  // the component itself; cast back to its own type only
};

class Registry {
public:
    Registry() = default;
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    template <class C>
    void add(C& component) {
        ComponentBase& base = component;
        base.registry_ = this;
        base.name_ = typeName<C>();
        components_.push_back(&base);
        bindings_.emplace(typeId<C>(), Binding{&base, static_cast<void*>(&component)});
    }

    void init();
    void deinit();

    const Binding* find(TypeId id) const {
        auto it = bindings_.find(id);
        return it == bindings_.end() ? nullptr : &it->second;
    }

private:
    enum class State { building, initialized, failed, deinitialized };

    void rollbackInit();

    std::vector<ComponentBase*> components_;  // in init order
    std::unordered_map<TypeId, Binding> bindings_;
    State state_ = State::building;
};

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

template <class C>
struct Slot {
    template <class... Args>
    explicit Slot(Args&... args) : component(construct<C>(args...)) {}
    C component;
};

template <class List>
struct Storage;
template <class... Cs>
struct Storage<TypeList<Cs...>> : Slot<Cs>... {
    // Bases are constructed in list order and destroyed in reverse.
    template <class... Args>
    explicit Storage(Args&... args) : Slot<Cs>(args...)... {}

    template <class T>
    T& get() noexcept {
        return static_cast<Slot<T>&>(*this).component;
    }
};

}  // namespace detail

// ===========================================================================
// keel::Component

/// Base class of every component. List the components it uses:
///   class Exporter : public keel::Component<Aggregator, Logger>
/// The listed types may be incomplete (forward-declared) in the header.
template <class... Dependencies>
class Component : public detail::ComponentBase {
public:
    using DeclaredDependencies = detail::TypeList<Dependencies...>;

    static_assert(detail::UniqueImpl<Dependencies...>::value, "keel: a dependency is listed twice in keel::Component<...>");

protected:
    /// A dependency. Compiles only if T is listed in keel::Component<...>. Usable from
    /// init() on; resolve it there and keep the reference.
    template <class T>
    T& getComponent() const {
        static_assert(detail::contains<T, DeclaredDependencies>,
                      "keel: getComponent<T>() requires T to be listed in this component's keel::Component<...>");
        return *static_cast<T*>(lookup(detail::typeId<T>(), detail::typeName<T>()));
    }
};

// ===========================================================================
// keel::App and keel::compose

/// A statically composed application. init() runs in the order of the list.
template <class... Components>
class App {
public:
    template <class... Args>
    explicit App(Args&&... args) : storage_(args...) {
        (registry_.add(storage_.template get<Components>()), ...);
    }

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    ~App() { registry_.deinit(); }

    void init() { registry_.init(); }
    void deinit() { registry_.deinit(); }

    /// For the composition root.
    template <class T>
    T& getComponent() {
        static_assert(detail::contains<T, detail::TypeList<Components...>>, "keel: T is not a component of this App");
        return storage_.template get<T>();
    }

private:
    detail::Registry registry_;  // declared first: outlives the components
    detail::Storage<detail::TypeList<Components...>> storage_;
};

/// `auto app = keel::compose<Settings, Console, Greeter>(argc, argv);`
/// Each component constructible from the arguments receives them; the others are
/// default-constructed.
template <class... Components, class... Args>
App<Components...> compose(Args&&... args) {
    return App<Components...>(std::forward<Args>(args)...);
}

// ===========================================================================
// Implementation

namespace detail {

inline void* ComponentBase::lookup(TypeId id, std::string_view dependency) const {
    if (registry_ == nullptr) {
        throw Error(concat({"getComponent<", dependency,
                            ">() called before the component was part of an application (from its constructor?). "
                            "Dependencies are available from init() on."}));
    }
    const Binding* b = registry_->find(id);
    if (b == nullptr) throw Error(concat({name_, " uses ", dependency, ", which is not part of the application"}));
    switch (b->owner->state_) {
        case State::initialized:
            return b->object;
        case State::created:
            throw Error(concat({name_, " used ", dependency, " before it was initialized"}));
        case State::deinitialized:
            break;
    }
    throw Error(concat({name_, " used ", dependency, " after it was deinitialized"}));
}

inline void Registry::rollbackInit() {
    for (auto it = components_.rbegin(); it != components_.rend(); ++it) {
        ComponentBase& c = **it;
        if (c.state_ != ComponentBase::State::initialized) continue;
        c.deinit();
        c.state_ = ComponentBase::State::deinitialized;
    }
}

inline void Registry::init() {
    if (state_ != State::building) throw Error("keel: init() called more than once");
    state_ = State::failed;  // until proven otherwise
    try {
        for (ComponentBase* c : components_) {
            c->init();
            c->state_ = ComponentBase::State::initialized;
        }
    } catch (...) {
        rollbackInit();
        throw;
    }
    state_ = State::initialized;
}

inline void Registry::deinit() {
    if (state_ != State::initialized) return;
    state_ = State::deinitialized;
    rollbackInit();  // deinitializes every initialized component, in reverse order
}

}  // namespace detail

}  // namespace keel
