#pragma once

// A deliberately small test harness: KR_TEST registers a case, KR_EXPECT*
// record failures without stopping the case, KR_ASSERT* stop it.

#include <format>
#include <functional>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace kr::testing {

struct TestCase {
  std::string_view name;
  std::function<void()> body;
};

std::vector<TestCase>& Registry();
void ReportFailure(std::string_view message, std::source_location where);

struct AssertionStop {};

struct Registrar {
  Registrar(std::string_view name, std::function<void()> body) {
    Registry().push_back({name, std::move(body)});
  }
};

template <typename A, typename B>
bool ExpectEq(const A& actual, const B& expected, std::string_view actual_text,
              std::string_view expected_text, std::source_location where) {
  if (actual == expected) return true;
  if constexpr (std::formattable<A, char> && std::formattable<B, char>) {
    ReportFailure(std::format("{} == {}\n    actual:   {}\n    expected: {}",
                              actual_text, expected_text, actual, expected),
                  where);
  } else {
    ReportFailure(std::format("{} == {}", actual_text, expected_text), where);
  }
  return false;
}

}  // namespace kr::testing

#define KR_TEST_CONCAT_INNER(a, b) a##b
#define KR_TEST_CONCAT(a, b) KR_TEST_CONCAT_INNER(a, b)

#define KR_TEST(name)                                                       \
  static void name();                                                       \
  static ::kr::testing::Registrar KR_TEST_CONCAT(kr_registrar_, name){#name, \
                                                                    &name}; \
  static void name()

#define KR_EXPECT(cond)                                                   \
  do {                                                                    \
    if (!(cond))                                                          \
      ::kr::testing::ReportFailure(#cond, std::source_location::current()); \
  } while (false)

#define KR_EXPECT_EQ(actual, expected)                                         \
  ::kr::testing::ExpectEq((actual), (expected), #actual, #expected,            \
                          std::source_location::current())

#define KR_ASSERT(cond)                                                     \
  do {                                                                      \
    if (!(cond)) {                                                          \
      ::kr::testing::ReportFailure(#cond, std::source_location::current()); \
      throw ::kr::testing::AssertionStop{};                                 \
    }                                                                       \
  } while (false)
