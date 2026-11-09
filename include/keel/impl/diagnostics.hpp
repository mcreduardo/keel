// The compile-time diagnostics: one class template per mistake, and Passed<...>.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include "type_list.hpp"

#include <type_traits>

namespace keel {

namespace detail {

// A failure is instantiated only if it is the mistake: one mistake, one error. compose<...>
// derives from the outcomes, isValid<...> only tests isPassed<>, so the two can't disagree.
// Each failure asserts a variable template that is false and read only when the check fails.
template <class... Context>
struct Passed {};

template <class Outcome>
inline constexpr bool isPassed = false;
template <class... Context>
inline constexpr bool isPassed<Passed<Context...>> = true;

// Carried as a type: returning the failure by value would instantiate it.
template <class Result>
using Outcome = std::type_identity<Result>;

// The component's constructor and compose<...> share this instantiation: a duplicate is one error.
template <class Base>
struct ComponentListCheck {
    static_assert(Base::uniqueDependencies,
                  "keel: a dependency is listed twice in keel::Component<...> (T, keel::Optional<T>, "
                  "keel::After<T>, keel::All<T> and keel::Optional<keel::All<T>> count as the same)");
    static_assert(Base::uniqueListedAlternatives,
                  "keel: keel::Alternative<I> is listed twice in keel::Component<...>, which lists only what the "
                  "component needs. Inherit keel::Alternative<I> instead, once: class C : public "
                  "keel::Component<...>, public keel::Alternative<I>, public I, public keel::Conditional");
    static_assert(Base::uniqueListedProvides,
                  "keel: keel::Provides<I> is listed twice in keel::Component<...>, which lists only what the "
                  "component needs. Inherit keel::Provides<I> instead, once: class C : public "
                  "keel::Component<...>, public keel::Provides<I>, public I");
    static_assert(Base::uniqueListedAsync,
                  "keel: keel::AsyncInit or keel::AsyncStart is listed twice in keel::Component<...>, which lists "
                  "only dependencies. Inherit it instead, once: class C : public keel::Component<...>, public "
                  "keel::AsyncInit");
};

template <class Component, class Dependency>
inline constexpr bool dependencyComposed = false;
template <class Component, class Dependency>
struct MissingDependency {
    static_assert(dependencyComposed<Component, Dependency>,
                  "keel: missing dependency: the Component in MissingDependency<Component, Dependency> needs "
                  "Dependency, and no composed component is or provides it.");
};

template <class Component, class Interface, class Providers>
inline constexpr bool singleProvider = false;
template <class Component, class Interface, class Providers>
struct AmbiguousDependency {
    static_assert(singleProvider<Component, Interface, Providers>,
                  "keel: ambiguous dependency: the Component in AmbiguousDependency<Component, Interface, "
                  "TypeList<Providers...>> needs one Interface, and each of the Providers is or provides it. "
                  "Compose only one of them, depend on the one you want by its own type, depend on "
                  "keel::All<Interface> to use all of them, or let configuration choose: derive "
                  "each of them from keel::Alternative<Interface> instead of keel::Provides<Interface>, and from "
                  "keel::Conditional, and enable one at runtime.");
};

template <class Interface, class Provides, class Alternatives>
inline constexpr bool unmixedProviders = false;
template <class Interface, class Provides, class Alternatives>
struct MixedProviders {
    static_assert(unmixedProviders<Interface, Provides, Alternatives>,
                  "keel: something needs one Interface, and its providers in MixedProviders<Interface, "
                  "TypeList<Provides...>, TypeList<Alternatives...>> are mixed: the Provides are Interface or derive "
                  "from keel::Provides<Interface>, the Alternatives derive from keel::Alternative<Interface>. "
                  "Derive each of them from keel::Alternative<Interface> (instead of keel::Provides<Interface>) and "
                  "from keel::Conditional, to choose one at runtime, or compose only one.");
};

template <class Component, class Interface>
inline constexpr bool anyProvider = false;
template <class Component, class Interface>
struct MissingProvider {
    static_assert(anyProvider<Component, Interface>,
                  "keel: the Component in MissingProvider<Component, Interface> lists keel::All<Interface>, which "
                  "needs at least one provider, and no composed component (other than Component itself) is or "
                  "provides Interface. Compose one, or depend on keel::Optional<keel::All<Interface>> if none is "
                  "fine.");
};

template <class Component, class Interface, class Providers>
inline constexpr bool unconditionalProvider = false;
template <class Component, class Interface, class Providers>
struct OnlyConditionalProviders {
    static_assert(unconditionalProvider<Component, Interface, Providers>,
                  "keel: the Component in OnlyConditionalProviders<Component, Interface, TypeList<Providers...>> "
                  "lists keel::All<Interface>, which is never empty, and each of the Providers of Interface is a "
                  "keel::Conditional that configuration may turn off. Depend on "
                  "keel::Optional<keel::All<Interface>> if none is fine, or compose a provider that isn't "
                  "conditional.");
};

template <class Component, class T>
inline constexpr bool unconditionalDependency = false;
template <class Component, class T>
struct RequiredConditional {
    static_assert(unconditionalDependency<Component, T>,
                  "keel: the Component in RequiredConditional<Component, T> requires T, and T is a "
                  "keel::Conditional that configuration may turn off. Depend on keel::Optional<T> instead, and "
                  "use findComponent<T>(), which returns nullptr while T is disabled (or keel::After<T> for "
                  "ordering only).");
};

template <class Component, class Interface, class Provider>
inline constexpr bool unconditionalInterface = false;
template <class Component, class Interface, class Provider>
struct RequiredConditionalProvider {
    static_assert(unconditionalInterface<Component, Interface, Provider>,
                  "keel: the Component in RequiredConditionalProvider<Component, Interface, Provider> requires "
                  "Interface, and its one provider, Provider, is a keel::Conditional that configuration may turn "
                  "off. Depend on keel::Optional<Interface> instead, or compose a provider that is always there "
                  "(to choose one of several at runtime, make each a keel::Alternative<Interface> and a "
                  "keel::Conditional).");
};

template <class Component>
inline constexpr bool conditionalAlternative = false;
template <class Component>
struct AlternativeNotConditional {
    static_assert(conditionalAlternative<Component>,
                  "keel: the Component in AlternativeNotConditional<Component> derives from keel::Alternative<I>, "
                  "so configuration decides whether it is the one, but it doesn't derive from keel::Conditional. "
                  "Derive from keel::Conditional publicly and override `bool enabled() const`, or derive from "
                  "keel::Provides<I> instead if it is always the one.");
};

template <class Component, class Interface>
inline constexpr bool providesOrAlternative = false;
template <class Component, class Interface>
struct ProvidesAndAlternative {
    static_assert(providesOrAlternative<Component, Interface>,
                  "keel: the Component in ProvidesAndAlternative<Component, Interface> derives from "
                  "keel::Provides<Interface> and from keel::Alternative<Interface>. Keep one: "
                  "keel::Alternative<Interface> if configuration chooses it among other implementations, "
                  "keel::Provides<Interface> if it is always the one.");
};

template <class Component, class Interface>
inline constexpr bool wellNestedAll = false;
template <class Component, class Interface>
struct MisnestedAll {
    static_assert(wellNestedAll<Component, Interface>,
                  "keel: the Component in MisnestedAll<Component, Interface> lists "
                  "keel::All<keel::Optional<Interface>>; write keel::Optional<keel::All<Interface>> for a list "
                  "that may be empty");
};

template <class Component, class T, class Derived>
inline constexpr bool providerDeclared = false;
template <class Component, class T, class Derived>
struct UndeclaredProvider {
    static_assert(providerDeclared<Component, T, Derived>,
                  "keel: the Component in UndeclaredProvider<Component, T, Derived> depends on T, nothing "
                  "composed is or provides T, and Derived derives from T without deriving from keel::Provides<T>. "
                  "Dependencies match by exact type: derive Derived from keel::Provides<T> too (publicly), or "
                  "depend on Derived.");
};

template <class Component, class T, class Derived>
inline constexpr bool subclassDeclared = false;
template <class Component, class T, class Derived>
struct UndeclaredSubclass {
    static_assert(subclassDeclared<Component, T, Derived>,
                  "keel: the Component in UndeclaredSubclass<Component, T, Derived> depends on T, which isn't "
                  "composed. Derived is a subclass of the component T, but keel matches exact types: compose T "
                  "itself, or derive T from keel::Provides<T> (publicly) so its subclasses provide T.");
};

template <class Component, class T, class Derived>
inline constexpr bool providerListed = false;
template <class Component, class T, class Derived>
struct LeftOutOfAll {
    static_assert(providerListed<Component, T, Derived>,
                  "keel: the Component in LeftOutOfAll<Component, T, Derived> lists keel::All<T> (or "
                  "keel::Optional<keel::All<T>>), and Derived derives from T but would be left out: keel matches "
                  "exact types. Derive Derived from keel::Provides<T> too (for a subclass of the component T, "
                  "derive T itself from it), or stop deriving from T.");
};

template <class Component, class Interface>
inline constexpr bool derivesFromInterface = false;
template <class Component, class Interface>
struct NotDerivedFromInterface {
    static_assert(derivesFromInterface<Component, Interface>,
                  "keel: the Component in NotDerivedFromInterface<Component, Interface> derives from "
                  "keel::Provides<Interface> (or keel::Alternative<Interface>) but doesn't derive from "
                  "Interface publicly and unambiguously (or Interface isn't defined at this point)");
};

template <class Component, class Interface>
inline constexpr bool accessibleProvides = false;
template <class Component, class Interface>
struct InaccessibleProvides {
    static_assert(accessibleProvides<Component, Interface>,
                  "keel: the Component in InaccessibleProvides<Component, Interface> derives from "
                  "keel::Provides<Interface> privately, protectedly or more than once, so keel can't see that it "
                  "provides Interface. Derive from keel::Provides<Interface> publicly, once (a subclass of a "
                  "provider inherits it): class C : public keel::Component<...>, public keel::Provides<Interface>, "
                  "public Interface");
};

template <class Component, class Interface>
inline constexpr bool accessibleAlternative = false;
template <class Component, class Interface>
struct InaccessibleAlternative {
    static_assert(accessibleAlternative<Component, Interface>,
                  "keel: the Component in InaccessibleAlternative<Component, Interface> derives from "
                  "keel::Alternative<Interface> privately, protectedly or more than once, so keel can't see that it "
                  "is an alternative of Interface. Derive from keel::Alternative<Interface> publicly, once: class C "
                  ": public keel::Component<...>, public keel::Alternative<Interface>, public Interface, public "
                  "keel::Conditional");
};

template <class Component, class Interface>
inline constexpr bool notListedAlternative = false;
template <class Component, class Interface>
struct AlternativeAsDependency {
    static_assert(notListedAlternative<Component, Interface>,
                  "keel: the Component in AlternativeAsDependency<Component, Interface> lists "
                  "keel::Alternative<Interface> in its keel::Component<...>, which lists only what the component "
                  "needs. Inherit it instead, next to keel::Component<...>: class Component : public "
                  "keel::Component<...>, public keel::Alternative<Interface>, public Interface, public "
                  "keel::Conditional");
};

template <class Component, class Interface>
inline constexpr bool notListedProvides = false;
template <class Component, class Interface>
struct ProvidesAsDependency {
    static_assert(notListedProvides<Component, Interface>,
                  "keel: the Component in ProvidesAsDependency<Component, Interface> lists keel::Provides<Interface> "
                  "in its keel::Component<...>, which lists only what the component needs. Inherit it instead, next "
                  "to keel::Component<...>: class Component : public keel::Component<...>, public "
                  "keel::Provides<Interface>, public Interface");
};

// One error per mistaken provider of one interface.
template <class... Failures>
struct ProviderFailures : Failures... {};

template <class Component, class Entry>
inline constexpr bool validEntry = false;
template <class Component, class Entry>
struct InvalidDependency {
    static_assert(validEntry<Component, Entry>,
                  "keel: the Component in InvalidDependency<Component, Entry> lists Entry (T, keel::Optional<T>, "
                  "keel::After<T> or keel::All<T>), and T can never be a component or an interface: T must be "
                  "a class type, not a reference, not const/volatile, and not itself keel::Optional, "
                  "keel::After, keel::Provides, keel::Alternative or keel::All");
};

template <class Component, class Entry>
inline constexpr bool asyncMarkerOnItsOwn = false;
template <class Component, class Entry>
struct MisplacedAsync {
    static_assert(asyncMarkerOnItsOwn<Component, Entry>,
                  "keel: the Component in MisplacedAsync<Component, Entry> lists keel::AsyncInit or "
                  "keel::AsyncStart inside Entry. They aren't dependencies but base classes: class C : public "
                  "keel::Component<Config>, public keel::AsyncInit");
};

template <class Component, class Marker>
inline constexpr bool asyncNotListed = false;
template <class Component, class Marker>
struct AsyncAsDependency {
    static_assert(asyncNotListed<Component, Marker>,
                  "keel: the Component in AsyncAsDependency<Component, Marker> lists the Marker (keel::AsyncInit or "
                  "keel::AsyncStart) in its keel::Component<...>, which lists only dependencies. Inherit it "
                  "instead, next to keel::Component<...>: class Component : public keel::Component<...>, public "
                  "Marker");
};

template <class Component, class Bundle>
inline constexpr bool notABundle = false;
template <class Component, class Bundle>
struct BundleAsDependency {
    static_assert(notABundle<Component, Bundle>,
                  "keel: the Component in BundleAsDependency<Component, Bundle> lists a keel::Bundle<...> in its "
                  "keel::Component<...> (as T, keel::Optional<T>, keel::After<T>, ...). Bundles only group "
                  "components for compose<...>: list the components you use, each on its own.");
};

template <class Bundle>
inline constexpr bool bundleOnce = false;
template <class Bundle>
struct DuplicateBundle {
    static_assert(bundleOnce<Bundle>,
                  "keel: the Bundle in DuplicateBundle<Bundle> is reached more than once in compose<...> (listed "
                  "twice, or also inside another bundle). List it once.");
};

template <class Cycle>
inline constexpr bool acyclic = false;
template <class Cycle>
struct DependencyCycle {
    static_assert(acyclic<Cycle>, "keel: dependency cycle among the components in DependencyCycle<TypeList<...>>");
};

template <class T>
inline constexpr bool isComponentType = false;
template <class T>
struct NotAComponent {
    static_assert(isComponentType<T>,
                  "keel: the T in NotAComponent<T> is in compose<...> but doesn't derive publicly, and once, "
                  "from keel::Component<...> (or isn't defined at this point). A keel::Bundle<...> is expanded "
                  "only when listed as is: not const, and not through a class derived from it");
};

template <class T>
inline constexpr bool listedOnce = false;
template <class T>
struct DuplicateComponent {
    static_assert(listedOnce<T>,
                  "keel: the T in DuplicateComponent<T> is listed more than once in compose<...> (counting the "
                  "components of each keel::Bundle<...>: two bundles may both contain T)");
};

template <class T>
inline constexpr bool concreteType = false;
template <class T>
struct AbstractComponent {
    static_assert(concreteType<T>,
                  "keel: the T in AbstractComponent<T> is abstract: compose concrete components. (A "
                  "keel::Conditional must override `bool enabled() const`, const included.)");
};

template <class T>
inline constexpr bool accessibleConditional = false;
template <class T>
struct InaccessibleConditional {
    static_assert(accessibleConditional<T>,
                  "keel: the T in InaccessibleConditional<T> derives from keel::Conditional privately, protectedly "
                  "or more than once, so keel can't ask its enabled(). Derive from keel::Conditional publicly, "
                  "once.");
};

// The empty pack only makes the assertion depend on a template argument.
template <class... None>
struct EmptyComposition {
    static_assert(sizeof...(None) > 0, "keel: compose<...> needs at least one component");
};

template <class T>
inline constexpr bool accessibleAsync = false;
template <class T>
struct InaccessibleAsync {
    static_assert(accessibleAsync<T>,
                  "keel: the T in InaccessibleAsync<T> derives from keel::AsyncInit or keel::AsyncStart privately, "
                  "protectedly or more than once, so keel can't see it. Derive from it publicly, once: class C : "
                  "public keel::Component<...>, public keel::AsyncInit");
};

// Converts to anything and supports the usual operators, so the mistake in fn is the only
// error, e.g. in `return app.run(...);`. Never defined.
struct RunMistake {
    template <class T>
    operator T() const;

#define KEEL_RUN_MISTAKE_OPERATOR(op)                         \
    template <class T>                                        \
    friend RunMistake operator op(const RunMistake&, const T&); \
    template <class T>                                        \
    friend RunMistake operator op(const T&, const RunMistake&);
    KEEL_RUN_MISTAKE_OPERATOR(==)
    KEEL_RUN_MISTAKE_OPERATOR(!=)
    KEEL_RUN_MISTAKE_OPERATOR(<)
    KEEL_RUN_MISTAKE_OPERATOR(>)
    KEEL_RUN_MISTAKE_OPERATOR(<=)
    KEEL_RUN_MISTAKE_OPERATOR(>=)
    KEEL_RUN_MISTAKE_OPERATOR(+)
    KEEL_RUN_MISTAKE_OPERATOR(-)
    KEEL_RUN_MISTAKE_OPERATOR(*)
    KEEL_RUN_MISTAKE_OPERATOR(/)
    KEEL_RUN_MISTAKE_OPERATOR(%)
#undef KEEL_RUN_MISTAKE_OPERATOR
};

template <class T>
struct NotInApp {
    static_assert(alwaysFalse<T>,
                  "keel: getComponent<T>(): the T in NotInApp<T> is neither a component of this App nor provided "
                  "by one. Keel matches exact types: a component that derives from T must also derive from "
                  "keel::Provides<T> (for a subclass of a component T, T itself must)");
};
template <class Interface, class Providers>
struct AmbiguousInterface {
    static_assert(alwaysFalse<Interface>,
                  "keel: getComponent<I>(): each of the Providers in AmbiguousInterface<I, TypeList<Providers...>> "
                  "provides I: get a specific one by its own type (if I itself is one of them, compose only one), "
                  "or make each of them a keel::Alternative<I> and a keel::Conditional");
};

template <class C, class MemberFunction>
struct RunTargetNotComposed : RunMistake {
    static_assert(alwaysFalse<C>,
                  "keel: run(&C::f) calls f on the component C (or on the one component that provides C), and "
                  "the C in RunTargetNotComposed<C, F> is neither in this App's compose<...> list nor provided by "
                  "one of its components. Keel matches exact types, and a member function inherited from a base "
                  "class names the base: derive the component from keel::Provides<Base> too, or call it from a "
                  "lambda");
};
template <class Interface, class Providers>
struct RunTargetAmbiguous : RunMistake {
    static_assert(alwaysFalse<Interface>,
                  "keel: run(&I::f) calls f on the one component that provides I, and each of the Providers in "
                  "RunTargetAmbiguous<I, TypeList<Providers...>> provides it: call it from a lambda on the one "
                  "you want, or make each of them a keel::Alternative<I> and a keel::Conditional");
};
// As a base, the composition mistake run(&C::f) runs into is reported once with the others.
template <class Mistake>
struct RunTargetMistake : RunMistake, Mistake {};
template <class C, class Provider>
struct RunTargetConditional : RunMistake {
    static_assert(alwaysFalse<C>,
                  "keel: run(&C::f) calls f on the one component that is or provides C, and the Provider in "
                  "RunTargetConditional<C, Provider> is a keel::Conditional that configuration may turn off. "
                  "Call it from a lambda that checks first (app.getComponent<C>() throws keel::Error while it "
                  "is disabled), or run a member function of a component that is always there");
};
template <class MemberFunction>
struct RunTargetNotCallable : RunMistake {
    static_assert(alwaysFalse<MemberFunction>,
                  "keel: run(&C::f) calls f on the component (an lvalue C&) with no arguments, and the F in "
                  "RunTargetNotCallable<F> can't be called that way");
};
template <class Fn>
struct RunNotCallable : RunMistake {
    static_assert(alwaysFalse<Fn>,
                  "keel: run(fn) needs fn callable with no arguments or with the App (App&), or a member function "
                  "of a component (&C::f); the Fn in RunNotCallable<Fn> is neither");
};
template <class T>
struct NotAnApp : RunMistake {
    static_assert(alwaysFalse<T>, "keel: keel::run<AppType>(...) needs a keel::App<...>; the T in NotAnApp<T> isn't one");
};

}  // namespace detail

}  // namespace keel
