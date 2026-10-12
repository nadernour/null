#include "core/platform.h"
#include <filesystem>

#if defined(NE_PLATFORM_WINDOWS)
#include <windows.h>
#elif defined(NE_PLATFORM_LINUX)
#include <limits.h>
#include <unistd.h>
#endif

namespace ne::platform {

std::string getExecutableDirectory() {
#if defined(NE_PLATFORM_WINDOWS)
  wchar_t path[MAX_PATH];
  GetModuleFileNameW(NULL, path, MAX_PATH);
  return std::filesystem::path(path).parent_path().string();
#elif defined(NE_PLATFORM_LINUX)
  char result[PATH_MAX];
  ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
  if (count > 0) {
    return std::filesystem::path(std::string(result, count)).parent_path().string();
  }
  return std::filesystem::current_path().string();
#else
  return std::filesystem::current_path().string();
#endif
}

} // namespace ne::platform
