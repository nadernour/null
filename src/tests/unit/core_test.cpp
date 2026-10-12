#ifndef NE_BUILD_SHIPPING

#include "tests/test_runner.h"
#include "core/event.h"
#include "core/filesystem.h"
#include "core/platform.h"
#include "core/math/math.h"

namespace ne::test {

NE_TEST_CASE("core", "Exceptions Disabled Check") {
#if defined(__cpp_exceptions)
  bool exceptionsDisabled = false;
#else
  bool exceptionsDisabled = true;
#endif

  NE_TEST_ASSERT(exceptionsDisabled, "Exceptions should be always disabled for optimal performance.");
}

NE_TEST_CASE("core", "Event Bus Lifecycle & Dispatch") {
  // Basic dispatch
  Event<int, float> testEvent;
  NE_TEST_ASSERT(testEvent.empty(), "Event must start empty.");
  NE_TEST_ASSERT(testEvent.size() == 0, "Event size must be 0.");

  int receivedInt = 0;
  float receivedFloat = 0.0f;
  CallbackId id1 = testEvent.add([&](int iInt, float iFloat) {
    receivedInt = iInt;
    receivedFloat = iFloat;
  });

  NE_TEST_ASSERT(!testEvent.empty(), "Event must not be empty after add.");
  NE_TEST_ASSERT(testEvent.size() == 1, "Event size must be 1.");

  testEvent.broadcast(42, 3.14f);
  NE_TEST_ASSERT(receivedInt == 42 && math::equals(receivedFloat, 3.14f), "Callback received broadcast arguments.");

  // Multiple listeners
  int listener2Count = 0;
  testEvent.add([&](int, float) { listener2Count++; });

  testEvent.broadcast(1, 2.0f);
  NE_TEST_ASSERT(receivedInt == 1 && listener2Count == 1, "Both listeners invoked on broadcast.");
  NE_TEST_ASSERT(testEvent.size() == 2, "Event size must be 2.");

  // Remove single listener
  bool removed = testEvent.remove(id1);
  NE_TEST_ASSERT(removed, "remove() must return true for existing CallbackId.");
  NE_TEST_ASSERT(testEvent.size() == 1, "Event size decrements after remove.");

  receivedInt = 999;
  testEvent.broadcast(100, 200.0f);
  NE_TEST_ASSERT(receivedInt == 999, "Removed listener must not be called.");
  NE_TEST_ASSERT(listener2Count == 2, "Remaining listener must still receive broadcast.");

  // Clear all listeners
  testEvent.clear();
  NE_TEST_ASSERT(testEvent.empty(), "Event must be empty after clear().");
  NE_TEST_ASSERT(testEvent.size() == 0, "Event size must be 0 after clear().");
}

NE_TEST_CASE("core", "Platform & Filesystem Path Resolution") {
  std::string exeDir = platform::getExecutableDirectory();
  NE_TEST_ASSERT(!exeDir.empty(), "platform::getExecutableDirectory must return non-empty string.");

  std::string contentPath = fs::resolveContentPath("models/Box.gltf");
  NE_TEST_ASSERT(!contentPath.empty(), "fs::resolveContentPath must return non-empty path.");
  NE_TEST_ASSERT(contentPath.find("Box.gltf") != std::string::npos, "Resolved content path must contain target filename.");

  std::string shaderPath = fs::resolveShaderPath("base_shader");
  NE_TEST_ASSERT(!shaderPath.empty(), "fs::resolveShaderPath must return non-empty path.");
  NE_TEST_ASSERT(shaderPath.find("base_shader.spv") != std::string::npos, "Resolved shader path must append .spv extension.");

  std::string savedPath = fs::resolveSavedPath("imgui.ini");
  NE_TEST_ASSERT(!savedPath.empty(), "fs::resolveSavedPath must return non-empty path.");
  NE_TEST_ASSERT(savedPath.find("imgui.ini") != std::string::npos, "Resolved saved path must contain target filename.");
}

} // namespace ne::test

#endif
