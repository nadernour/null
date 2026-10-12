#pragma once

#ifndef NE_BUILD_SHIPPING

#include "core/logger.h"
#include <cstdint>
#include <string>
#include <vector>

namespace ne {
class Application;
}

namespace ne::test {

struct TestContext {
  Application* app{nullptr}; // For tests that need the device or GPU resources
  uint32_t passes{0};
  uint32_t failures{0};

  bool isSuccess() const { return failures == 0; }
};

using TestFn = void (*)(TestContext& ioContext);

struct TestCase {
  const char* suiteName;
  const char* testName;
  const char* file;
  int line;
  TestFn func;
};

class TestRegistry {
public:
  static TestRegistry& get();

  void registerTest(const TestCase& iTestCase);
  const std::vector<TestCase>& getTests() const;
  int runAll(Application& ioApp);

private:
  std::vector<TestCase> mTests;
};

struct TestRegistrar {
  TestRegistrar(const char* iSuiteName, const char* iTestName, const char* iFile, int iLine, TestFn iFunc) {
    TestRegistry::get().registerTest({iSuiteName, iTestName, iFile, iLine, iFunc});
  }
};

int runAllTests(Application& ioApp);

} // namespace ne::test

#define NE_CONCAT_IMPL(a, b) a##b
#define NE_CONCAT(a, b) NE_CONCAT_IMPL(a, b)

#define NE_TEST_CASE(SuiteName, TestName)                                                                                        \
  static void NE_CONCAT(ne_test_func_, __LINE__)(::ne::test::TestContext & ioContext);                                           \
  static ::ne::test::TestRegistrar NE_CONCAT(ne_test_reg_, __LINE__)(SuiteName, TestName, __FILE__, __LINE__,                    \
                                                                     &NE_CONCAT(ne_test_func_, __LINE__));                       \
  static void NE_CONCAT(ne_test_func_, __LINE__)(::ne::test::TestContext & ioContext)
#endif

// you can only use it inside NE_TEST_CASE
#define NE_TEST_ASSERT(condition, ...)                                                                                           \
  do {                                                                                                                           \
    if (condition) {                                                                                                             \
      ioContext.passes++;                                                                                                        \
    } else {                                                                                                                     \
      ioContext.failures++;                                                                                                      \
      std::string user_msg;                                                                                                      \
      __VA_OPT__(user_msg = std::format(__VA_ARGS__);)                                                                           \
      ne::Logger::log(ne::Logger::LogLevel::Error, "[TEST FAIL] {}\n  Condition: {}\n  File: {}\n  Line: {}", user_msg,          \
                      #condition, __FILE__, __LINE__);                                                                           \
    }                                                                                                                            \
  } while (false)
