// Must not compile: in a 150-component app listed backwards, Link<0> and Link<1> depend on
// each other, and every other link depends on them through the chain. The error names
// only the two (finding them must stay within the compiler's constexpr step limit).
#include <keel.hpp>

#include <cstddef>
#include <utility>

template <int N>
class Link;

// Link<N> uses Link<N-1> ... Link<N-4>.
template <int N>
struct LinkBase {
    using type = keel::Component<Link<N - 1>, Link<N - 2>, Link<N - 3>, keel::Optional<Link<N - 4>>>;
};
template <>
struct LinkBase<0> {
    using type = keel::Component<Link<1>>;
};
template <>
struct LinkBase<1> {
    using type = keel::Component<Link<0>>;
};
template <>
struct LinkBase<2> {
    using type = keel::Component<Link<1>, Link<0>>;
};
template <>
struct LinkBase<3> {
    using type = keel::Component<Link<2>, Link<1>, Link<0>>;
};

template <int N>
class Link : public LinkBase<N>::type {};

constexpr int appSize = 150;

template <std::size_t... Is>
auto reversedLinks(std::index_sequence<Is...>) -> keel::App<Link<appSize - 1 - static_cast<int>(Is)>...>;
using LargeApp = decltype(reversedLinks(std::make_index_sequence<appSize>{}));

template <class App>
inline constexpr bool validApp = true;
template <class... Cs>
inline constexpr bool validApp<keel::App<Cs...>> = keel::isValid<Cs...>;
static_assert(!validApp<LargeApp>, "isValid disagrees with compose");

int main() {
    LargeApp app;
}
