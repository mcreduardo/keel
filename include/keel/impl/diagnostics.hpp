// The compile-time diagnostics: one class template per mistake, and Passed.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eduardo Rocha
#pragma once

#include "type_list.hpp"

#include <array>
#include <cstddef>
#include <iterator>
#include <tuple>
#include <type_traits>
#include <utility>

namespace keel {

namespace detail {

// A failure is instantiated only if it is the mistake: one mistake, one error. compose<...>
// instantiates the analysis's list of outcomes (Report), isValid<...> tests that each is Passed
// (allPassed), so the two can't disagree. Each failure asserts fixed<itself>, false and read only
// when the check fails.
struct Passed {};
template <class Outcome>
inline constexpr bool isPassed = std::is_same_v<Outcome, Passed>;
template <class Failure>
inline constexpr bool fixed = false;

template <class... Outcomes>
inline constexpr std::size_t failureCount = [] {  // a loop: Clang caps folds at 256 operands
    constexpr bool passed[] = {true, isPassed<Outcomes>...};
    std::size_t n = 0;
    for (bool p : passed) n += p ? 0 : 1;
    return n;
}();
template <class Outcomes>
inline constexpr bool allPassed = false;
template <class... Outcomes>
inline constexpr bool allPassed<TypeList<Outcomes...>> = failureCount<Outcomes...> == 0;

// Each failure is the base of its own slot, so a repeated one is no duplicate base.
template <std::size_t I, class Failure>
struct Reported : Failure {};
template <class Failures, class Indices>
struct ReportAll;
template <class... Failures, std::size_t... Is>
struct ReportAll<TypeList<Failures...>, std::index_sequence<Is...>> : Reported<Is, Failures>... {};

// Only the failures, picked without recursion, so the notes name the mistakes, not every Passed.
template <class... Outcomes>
inline constexpr auto failureIndices = [] {
    constexpr bool passed[] = {true, isPassed<Outcomes>...};  // passed[i + 1] is Outcomes[i]
    std::array<std::size_t, failureCount<Outcomes...>> out{};
    std::size_t n = 0;
    for (std::size_t i = 1; i < std::size(passed); ++i) {
        if (!passed[i]) out[n++] = i - 1;
    }
    return out;
}();
template <class Outcomes, class Indices = void>
struct FailuresOf;
template <class... Outcomes>
struct FailuresOf<TypeList<Outcomes...>, void>
    : FailuresOf<TypeList<Outcomes...>, std::make_index_sequence<failureCount<Outcomes...>>> {};
template <class... Outcomes, std::size_t... Is>
struct FailuresOf<TypeList<Outcomes...>, std::index_sequence<Is...>> {
    using type = TypeList<std::tuple_element_t<failureIndices<Outcomes...>[Is], std::tuple<Outcomes...>>...>;
};
template <class Failures>
struct ReportOf;
template <class... Failures>
struct ReportOf<TypeList<Failures...>> {
    using type = ReportAll<TypeList<Failures...>, std::index_sequence_for<Failures...>>;
};
// What compose<...> instantiates: an alias, so the helpers above don't appear in the notes.
template <class Outcomes>
using Report = typename ReportOf<typename FailuresOf<Outcomes>::type>::type;

// Carried as a type: returning the failure by value would instantiate it.
template <class Result>
using Outcome = std::type_identity<Result>;

// The component's constructor and compose<...> share this instantiation: a duplicate is one error.
template <class Base>
struct ComponentListCheck {
    static_assert(Base::uniqueEntries,
                  "keel: a dependency is listed twice in keel::Component<...> (T, keel::Optional<T>, "
                  "keel::After<T>, keel::All<T>, keel::Optional<keel::All<T>> and keel::After<keel::All<T>> count "
                  "as the same)");
};

template <class Component, class Dependency>
struct MissingDependency {
    static_assert(fixed<MissingDependency>,
                  "keel: missing dependency: the Component in MissingDependency<Component, Dependency> needs "
                  "Dependency, and no composed component is or provides it.");
};

template <class Component, class Interface, class Providers>
struct AmbiguousDependency {
    static_assert(fixed<AmbiguousDependency>,
                  "keel: ambiguous dependency: the Component in AmbiguousDependency<Component, Interface, "
                  "TypeList<Providers...>> needs one Interface (as Interface, keel::Optional<Interface> or "
                  "keel::After<Interface>), and each of the Providers is or provides it. Compose only one of "
                  "them, depend on the one you want by its own type, depend on keel::All<Interface> to use all of "
                  "them (keel::After<keel::All<Interface>> to come after all of them), or let configuration "
                  "choose: derive each of them from keel::Alternative<Interface> instead of "
                  "keel::Provides<Interface>, and from keel::Conditional, and enable one at runtime.");
};

template <class Interface, class Provides, class Alternatives>
struct MixedProviders {
    static_assert(fixed<MixedProviders>,
                  "keel: something needs one Interface, and its providers in MixedProviders<Interface, "
                  "TypeList<Provides...>, TypeList<Alternatives...>> are mixed: the Provides are Interface or derive "
                  "from keel::Provides<Interface>, the Alternatives derive from keel::Alternative<Interface>. "
                  "Derive each of them from keel::Alternative<Interface> (instead of keel::Provides<Interface>) and "
                  "from keel::Conditional, to choose one at runtime, or compose only one.");
};

template <class Component, class Interface>
struct MissingProvider {
    static_assert(fixed<MissingProvider>,
                  "keel: the Component in MissingProvider<Component, Interface> lists keel::All<Interface>, which "
                  "needs at least one provider, and no composed component is or provides Interface. Compose one, "
                  "or depend on keel::Optional<keel::All<Interface>> if none is fine.");
};

template <class Component, class Interface, class Providers>
struct OnlyConditionalProviders {
    static_assert(fixed<OnlyConditionalProviders>,
                  "keel: the Component in OnlyConditionalProviders<Component, Interface, TypeList<Providers...>> "
                  "lists keel::All<Interface>, which is never empty, and each of the Providers of Interface is a "
                  "keel::Conditional that configuration may turn off. Depend on "
                  "keel::Optional<keel::All<Interface>> if none is fine, or compose a provider that isn't "
                  "conditional.");
};

template <class Component, class T, class Provider>
struct RequiredConditional {
    static_assert(fixed<RequiredConditional>,
                  "keel: the Component in RequiredConditional<Component, T, Provider> requires T, and Provider (T "
                  "itself, or its one provider) is a keel::Conditional that configuration may turn off. Depend on "
                  "keel::Optional<T> instead and use findComponent<T>(), which returns nullptr while it is "
                  "disabled (or keel::After<T> for ordering only), or compose a provider of T that is always "
                  "there (to choose one of several at runtime, make each a keel::Alternative<T> and a "
                  "keel::Conditional).");
};

template <class Component>
struct AlternativeNotConditional {
    static_assert(fixed<AlternativeNotConditional>,
                  "keel: the Component in AlternativeNotConditional<Component> derives from keel::Alternative<I>, "
                  "so configuration decides whether it is the one, but it doesn't derive from keel::Conditional. "
                  "Derive from keel::Conditional publicly and override `bool enabled() const`, or derive from "
                  "keel::Provides<I> instead if it is always the one.");
};

template <class Component, class Interface>
struct ProvidesAndAlternative {
    static_assert(fixed<ProvidesAndAlternative>,
                  "keel: the Component in ProvidesAndAlternative<Component, Interface> derives from "
                  "keel::Provides<Interface> and from keel::Alternative<Interface>. Keep one: "
                  "keel::Alternative<Interface> if configuration chooses it among other implementations, "
                  "keel::Provides<Interface> if it is always the one.");
};

template <class Component, class T, class Derived>
struct UndeclaredProvider {
    static_assert(fixed<UndeclaredProvider>,
                  "keel: the Component in UndeclaredProvider<Component, T, Derived> depends on T (as T, "
                  "keel::Optional<T>, keel::After<T>, keel::All<T>, ...), and Derived derives from T without "
                  "providing it: keel matches exact types, so Derived isn't used as a T. Derive Derived from "
                  "keel::Provides<T> too (publicly); if T is itself a component, compose T, or derive T from "
                  "keel::Provides<T> so that its subclasses provide it; or depend on Derived.");
};

template <class Component, class Interface>
struct NotDerivedFromInterface {
    static_assert(fixed<NotDerivedFromInterface>,
                  "keel: the Component in NotDerivedFromInterface<Component, Interface> derives from "
                  "keel::Provides<Interface> (or keel::Alternative<Interface>) but doesn't derive from "
                  "Interface publicly and unambiguously (or Interface isn't defined at this point)");
};

template <class Component, class Base>
struct InaccessibleBase {
    static_assert(fixed<InaccessibleBase>,
                  "keel: the Component in InaccessibleBase<Component, Base> derives from Base privately (the "
                  "default for class), protectedly or more than once, so keel can't see it. Derive from Base "
                  "publicly, once: class C : public keel::Component<...>, public Base; if a base class of "
                  "Component already derives from Base, don't derive from it again.");
};

// One error per mistaken provider of one interface.
template <class... Failures>
struct ProviderFailures : Failures... {};

template <class Component, class Entry>
struct InvalidDependency {
    static_assert(fixed<InvalidDependency>,
                  "keel: the Component in InvalidDependency<Component, Entry> lists Entry in its "
                  "keel::Component<...>, which lists only dependencies, and Entry isn't one. It is a keel marker "
                  "that is a base class (keel::Provides<I>, keel::Alternative<I>, keel::AsyncInit, "
                  "keel::AsyncStart: inherit it next to keel::Component<...>), a keel::Bundle<...> (list its "
                  "components), keel::All<keel::Optional<I>> (write keel::Optional<keel::All<I>>), or names a "
                  "type no dependency can name (T in T, keel::Optional<T>, keel::After<T> and keel::All<T> must be "
                  "a class type, not a reference, not const/volatile, and not itself a keel marker)");
};

template <class Bundle>
struct DuplicateBundle {
    static_assert(fixed<DuplicateBundle>,
                  "keel: the Bundle in DuplicateBundle<Bundle> is reached more than once in compose<...> (listed "
                  "twice, or also inside another bundle). List it once.");
};

template <class Cycle>
struct DependencyCycle {
    static_assert(fixed<DependencyCycle>,
                  "keel: dependency cycle among the components in DependencyCycle<TypeList<...>>");
};

template <class T>
struct NotAComponent {
    static_assert(fixed<NotAComponent>,
                  "keel: the T in NotAComponent<T> is in compose<...> but doesn't derive publicly, and once, "
                  "from keel::Component<...> (or isn't defined at this point). A keel::Bundle<...> is expanded "
                  "only when listed as is: not const, and not through a class derived from it");
};

template <class T>
struct DuplicateComponent {
    static_assert(fixed<DuplicateComponent>,
                  "keel: the T in DuplicateComponent<T> is listed more than once in compose<...> (counting the "
                  "components of each keel::Bundle<...>: two bundles may both contain T)");
};

template <class T>
struct AbstractComponent {
    static_assert(fixed<AbstractComponent>,
                  "keel: the T in AbstractComponent<T> is abstract: compose concrete components. (A "
                  "keel::Conditional must override `bool enabled() const`, const included.)");
};

// A template only so that the assertion is dependent.
template <class... None>
struct EmptyComposition {
    static_assert(fixed<EmptyComposition>, "keel: compose<...> needs at least one component");
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
    static_assert(fixed<NotInApp>,
                  "keel: getComponent<T>(): the T in NotInApp<T> is neither a component of this App nor provided "
                  "by one. Keel matches exact types: a component that derives from T must also derive from "
                  "keel::Provides<T> (for a subclass of a component T, T itself must)");
};
template <class Interface, class Providers>
struct AmbiguousInterface {
    static_assert(fixed<AmbiguousInterface>,
                  "keel: getComponent<I>(): each of the Providers in AmbiguousInterface<I, TypeList<Providers...>> "
                  "provides I: get a specific one by its own type (if I itself is one of them, compose only one), "
                  "or make each of them a keel::Alternative<I> and a keel::Conditional");
};

template <class C, class MemberFunction>
struct RunTargetNotComposed : RunMistake {
    static_assert(fixed<RunTargetNotComposed>,
                  "keel: run(&C::f) calls f on the component C (or on the one component that provides C), and "
                  "the C in RunTargetNotComposed<C, F> is neither in this App's compose<...> list nor provided by "
                  "one of its components. Keel matches exact types, and a member function inherited from a base "
                  "class names the base: derive the component from keel::Provides<Base> too, or call it from a "
                  "lambda");
};
template <class Interface, class Providers>
struct RunTargetAmbiguous : RunMistake {
    static_assert(fixed<RunTargetAmbiguous>,
                  "keel: run(&I::f) calls f on the one component that provides I, and each of the Providers in "
                  "RunTargetAmbiguous<I, TypeList<Providers...>> provides it: call it from a lambda on the one "
                  "you want, or make each of them a keel::Alternative<I> and a keel::Conditional");
};
// As a base, the composition mistake run(&C::f) runs into is reported once with the others.
template <class Mistake>
struct RunTargetMistake : RunMistake, Mistake {};
template <class C, class Provider>
struct RunTargetConditional : RunMistake {
    static_assert(fixed<RunTargetConditional>,
                  "keel: run(&C::f) calls f on the one component that is or provides C, and the Provider in "
                  "RunTargetConditional<C, Provider> is a keel::Conditional that configuration may turn off. "
                  "Call it from a lambda that checks first (app.getComponent<C>() throws keel::Error while it "
                  "is disabled), or run a member function of a component that is always there");
};
template <class MemberFunction>
struct RunTargetNotCallable : RunMistake {
    static_assert(fixed<RunTargetNotCallable>,
                  "keel: run(&C::f) calls f on the component (an lvalue C&) with no arguments, and the F in "
                  "RunTargetNotCallable<F> can't be called that way");
};
template <class Fn>
struct RunNotCallable : RunMistake {
    static_assert(fixed<RunNotCallable>,
                  "keel: run(fn) needs fn callable with no arguments or with the App (App&), or a member function "
                  "of a component (&C::f); the Fn in RunNotCallable<Fn> is neither");
};
template <class T>
struct NotAnApp : RunMistake {
    static_assert(fixed<NotAnApp>,
                  "keel: keel::run<AppType>(...) needs a keel::App<...>; the T in NotAnApp<T> isn't one");
};

}  // namespace detail

}  // namespace keel
