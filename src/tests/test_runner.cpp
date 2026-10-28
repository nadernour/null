#include "tests/test_runner.h"
#include <format>
#include <map>
#include <random>

#ifndef NE_BUILD_SHIPPING

namespace ne::test {

TestRegistry& TestRegistry::get() {
  static TestRegistry instance;
  return instance;
}

void TestRegistry::registerTest(const TestCase& iTestCase) { mTests.push_back(iTestCase); }

const std::vector<TestCase>& TestRegistry::getTests() const { return mTests; }

int TestRegistry::runAll(Application& ioApp) {
  NE_LOG("==================================================");
  NE_LOG("          STARTING NULL ENGINE TEST SUITE         ");
  NE_LOG("==================================================");

  TestContext totalCtx;
  std::map<std::string, std::vector<const TestCase*>> suiteMap;
  for (const auto& testCase : mTests) {
    suiteMap[testCase.suiteName].push_back(&testCase);
  }

  for (const auto& [suiteName, tests] : suiteMap) {
    NE_LOG("[RUNNING SUITE] {}", suiteName);
    TestContext suiteCtx{.app = &ioApp};
    for (const auto* testCase : tests) {
      NE_VERBOSE("  [RUNNING TEST] {}", testCase->testName);
      testCase->func(suiteCtx);
    }
    totalCtx.passes += suiteCtx.passes;
    totalCtx.failures += suiteCtx.failures;

    if (suiteCtx.isSuccess()) {
      NE_LOG("[PASSED] {} (Tests: {}, Checks Passed: {})", suiteName, tests.size(), suiteCtx.passes);
    } else {
      NE_WARN("[FAILED] {} (Tests: {}, Passed: {}, Failed: {})", suiteName, tests.size(), suiteCtx.passes, suiteCtx.failures);
    }
  }

  NE_LOG("==================================================");
  if (totalCtx.isSuccess()) {
    NE_LOG("ALL UNIT TEST SUITES PASSED (Total Tests: {}, Total Checks: {})", mTests.size(), totalCtx.passes);
    NE_LOG("==================================================");
    return 0;
  } else {
    NE_WARN("TEST SUITE FAILED (Total Tests: {}, Passed: {}, Failed: {})", mTests.size(), totalCtx.passes, totalCtx.failures);
    NE_LOG("==================================================");
    return 1;
  }
}

int runAllTests(Application& ioApp) { return TestRegistry::get().runAll(ioApp); }

TempDirectory::TempDirectory() {
  std::random_device random;
  // create_directory is false when another directory already has the name, so the loop draws another
  do {
    mPath = std::filesystem::temp_directory_path() / std::format("null_test_{:08x}", random());
  } while (!std::filesystem::create_directory(mPath));
}

TempDirectory::~TempDirectory() {
  std::error_code error;
  std::filesystem::remove_all(mPath, error);
  if (error) {
    NE_WARN("TempDirectory: Couldn't delete {}: {}", mPath.string(), error.message());
  }
}

} // namespace ne::test

#endif
