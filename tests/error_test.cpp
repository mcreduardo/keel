#include <keel.hpp>

#include "check.hpp"

#include <stdexcept>
#include <type_traits>

static_assert(std::is_base_of_v<std::logic_error, keel::Error>);

TEST(error_carries_its_message) {
    try {
        throw keel::Error("missing dependency");
    } catch (const std::logic_error& e) {
        CHECK(std::string(e.what()) == "missing dependency");
    }
}
