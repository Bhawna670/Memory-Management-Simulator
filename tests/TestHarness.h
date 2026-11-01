#pragma once

// Minimal dependency-free test harness.
//   CHECK(cond), CHECK_EQ(a, b), CHECK_THROWS(expr, ExceptionType)
//   RUN_TEST(fn) runs a void() test; TEST_SUMMARY() returns the process exit code.

#include <exception>
#include <iostream>
#include <type_traits>

namespace testing {

inline int& failureCount() {
    static int n = 0;
    return n;
}
inline int& checkCount() {
    static int n = 0;
    return n;
}

inline void check(bool ok, const char* expr, const char* file, int line) {
    ++checkCount();
    if (!ok) {
        ++failureCount();
        std::cerr << "  FAILED " << file << ":" << line << ": " << expr << "\n";
    }
}

// Equality that is safe for mixed signed/unsigned integers (no -Wsign-compare).
template <typename A, typename B>
bool equalValues(const A& a, const B& b) {
    if constexpr (std::is_integral_v<A> && std::is_integral_v<B>) {
        if constexpr (std::is_signed_v<A> == std::is_signed_v<B>) {
            return a == b;
        } else if constexpr (std::is_signed_v<A>) {
            return a >= 0 && static_cast<std::make_unsigned_t<A>>(a) == b;
        } else {
            return b >= 0 && a == static_cast<std::make_unsigned_t<B>>(b);
        }
    } else {
        return a == b;
    }
}

template <typename A, typename B>
void checkEq(const A& a, const B& b, const char* ea, const char* eb, const char* file, int line) {
    ++checkCount();
    if (!equalValues(a, b)) {
        ++failureCount();
        std::cerr << "  FAILED " << file << ":" << line << ": " << ea << " == " << eb
                  << "  (got " << a << " vs " << b << ")\n";
    }
}

inline void runTest(const char* name, void (*fn)()) {
    const int before = failureCount();
    try {
        fn();
    } catch (const std::exception& e) {
        ++failureCount();
        std::cerr << "  UNEXPECTED EXCEPTION: " << e.what() << "\n";
    } catch (...) {
        ++failureCount();
        std::cerr << "  UNEXPECTED NON-STD EXCEPTION\n";
    }
    std::cout << (failureCount() == before ? "[  OK  ] " : "[ FAIL ] ") << name << "\n";
}

inline int summary() {
    std::cout << "\n" << checkCount() << " checks, " << failureCount() << " failed\n";
    return failureCount() == 0 ? 0 : 1;
}

}  // namespace testing

#define CHECK(cond) ::testing::check((cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b) ::testing::checkEq((a), (b), #a, #b, __FILE__, __LINE__)
#define CHECK_THROWS(expr, ExType)                                                      \
    do {                                                                                \
        bool caught_ = false;                                                           \
        try {                                                                           \
            (void)(expr);                                                               \
        } catch (const ExType&) {                                                       \
            caught_ = true;                                                             \
        }                                                                               \
        ::testing::check(caught_, "throws " #ExType ": " #expr, __FILE__, __LINE__);    \
    } while (0)
#define RUN_TEST(fn) ::testing::runTest(#fn, fn)
#define TEST_SUMMARY() ::testing::summary()
