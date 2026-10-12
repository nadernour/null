#include "apps/basic_app.h"
#include "core/logger.h"

#include <string_view>

#ifndef NE_BUILD_SHIPPING
#include "tests/test_runner.h"
#endif

int main(int argc, char** argv) {
  [[maybe_unused]] bool runTests = false; // Shipping has no test runner
  bool verbose = false;
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--run-tests" || arg == "-t") {
      runTests = true;
    } else if (arg == "--verbose" || arg == "-v") {
      verbose = true;
    }
  }

  // Before the app is created, so its startup steps are logged too
  ne::Logger::init(verbose);
  ne::BasicApp app{};

#ifndef NE_BUILD_SHIPPING
  if (runTests) {
    NE_LOG("Executing engine unit tests via CLI flag...");
    return ne::test::runAllTests(app);
  }
#endif

  app.run();
  return 0;
}
