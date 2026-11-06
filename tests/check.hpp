// A minimal test harness, so the tests need nothing besides the standard library.
//
//   TEST(composes_in_order) {
//       CHECK(order == expected);
//       CHECK_THROWS(app.init());
//   }
#pragma once

#include <string>
#include <vector>

namespace check {

struct Case {
    const char* name;
    void (*run)();
};

inline std::vector<Case>& cases() {
    static std::vector<Case> all;
    return all;
}

struct Register {
    Register(const char* name, void (*run)()) { cases().push_back(Case{name, run}); }
};

struct Failure {
    std::string message;
};

inline std::string where(const char* file, int line) {
    return std::string(file) + ":" + std::to_string(line) + ": ";
}

}  // namespace check

#define TEST(name)                                                       \
    static void test_##name();                                           \
    static const ::check::Register register_##name(#name, &test_##name); \
    static void test_##name()

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) throw ::check::Failure{::check::where(__FILE__, __LINE__) + #condition}; \
    } while (false)

#define CHECK_THROWS(expression)                                                                         \
    do {                                                                                                 \
        bool threw_ = false;                                                                             \
        try {                                                                                            \
            expression;                                                                                  \
        } catch (...) {                                                                                  \
            threw_ = true;                                                                               \
        }                                                                                                \
        if (!threw_) throw ::check::Failure{::check::where(__FILE__, __LINE__) + "no exception: " #expression}; \
    } while (false)
