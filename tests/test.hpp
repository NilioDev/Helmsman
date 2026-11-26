// SPDX-License-Identifier: GPL-3.0-or-later
//
// A deliberately tiny test harness, so the core can be tested without
// third-party dependencies.
//
//   HLM_TEST(name) { HLM_CHECK(1 + 1 == 2); }
#pragma once

#include <functional>
#include <string_view>
#include <vector>

namespace helmsman::test {

struct Case {
    std::string_view name;
    std::function<void()> body;
};

std::vector<Case>& registry();
void reportFailure(const char* file, int line, const char* expression);
void reportSkip(const char* reason);

struct Registrar {
    Registrar(std::string_view name, std::function<void()> body) {
        registry().push_back({name, std::move(body)});
    }
};

} // namespace helmsman::test

#define HLM_TEST_CONCAT_INNER(a, b) a##b
#define HLM_TEST_CONCAT(a, b) HLM_TEST_CONCAT_INNER(a, b)

#define HLM_TEST(name)                                                                     \
    static void HLM_TEST_CONCAT(hlm_test_fn_, __LINE__)();                                 \
    static const ::helmsman::test::Registrar HLM_TEST_CONCAT(hlm_test_reg_, __LINE__){          \
        name, &HLM_TEST_CONCAT(hlm_test_fn_, __LINE__)};                                   \
    static void HLM_TEST_CONCAT(hlm_test_fn_, __LINE__)()

#define HLM_CHECK(...)                                                              \
    do {                                                                                   \
        if (!(__VA_ARGS__)) {                                                               \
            ::helmsman::test::reportFailure(__FILE__, __LINE__, #__VA_ARGS__);                   \
        }                                                                                  \
    } while (false)

// Checks that an expression throws (any exception).
#define HLM_CHECK_THROWS(...)                                                              \
    do {                                                                                   \
        bool hlmThrew = false;                                                             \
        try {                                                                              \
            static_cast<void>(__VA_ARGS__);                                                \
        } catch (...) {                                                                    \
            hlmThrew = true;                                                               \
        }                                                                                  \
        if (!hlmThrew) {                                                                   \
            ::helmsman::test::reportFailure(__FILE__, __LINE__, "throws: " #__VA_ARGS__);    \
        }                                                                                  \
    } while (false)

// Ends the current test early and reports it as skipped, for tests that need
// files which are not always present (such as the original game data).
#define HLM_SKIP(reason)                                                                   \
    do {                                                                                   \
        ::helmsman::test::reportSkip(reason);                                                   \
        return;                                                                            \
    } while (false)
