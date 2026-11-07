#include "krypton/testing/test.hpp"

#include <cstdio>
#include <print>

namespace kr::testing {
namespace {
int g_failures_in_case = 0;
}

std::vector<TestCase>& Registry() {
  static std::vector<TestCase> registry;
  return registry;
}

void ReportFailure(std::string_view message, std::source_location where) {
  ++g_failures_in_case;
  std::println(stderr, "  FAILED at {}:{}\n    {}", where.file_name(),
               where.line(), message);
}

}  // namespace kr::testing

int main(int argc, char** argv) {
  using namespace kr::testing;
  const std::string_view filter = argc > 1 ? argv[1] : "";
  int failed_cases = 0;
  int ran = 0;
  for (const TestCase& test : Registry()) {
    if (!filter.empty() && !test.name.contains(filter)) continue;
    ++ran;
    g_failures_in_case = 0;
    try {
      test.body();
    } catch (const AssertionStop&) {
    } catch (...) {
      ReportFailure("unexpected exception", std::source_location::current());
    }
    std::println("[{}] {}", g_failures_in_case == 0 ? " OK " : "FAIL", test.name);
    if (g_failures_in_case != 0) ++failed_cases;
  }
  std::println("{} of {} test(s) passed", ran - failed_cases, ran);
  return failed_cases == 0 ? 0 : 1;
}
