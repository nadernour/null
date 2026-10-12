#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <utility>

namespace ne {

// Thread-safe. Log lines go to stdout and the rest to stderr, colored on a terminal; every line also goes to
// logs/null_<start time>.log next to the executable, and the 10 newest log files are kept.
class Logger {
public:
  enum class LogType : uint8_t { Log, Warn, Error, Fatal };

  template <typename... Args> static void log(LogType iLogType, std::format_string<Args...> iFmt, Args&&... iArgs) {
    write(iLogType, std::format(iFmt, std::forward<Args>(iArgs)...));
  }

private:
  static void write(LogType iLogType, const std::string& iMessage);
};

} // namespace ne

#ifndef NE_BUILD_SHIPPING
#define NE_LOG(...) ne::Logger::log(ne::Logger::LogType::Log, __VA_ARGS__)
#define NE_WARN(...) ne::Logger::log(ne::Logger::LogType::Warn, __VA_ARGS__)
#define NE_ERROR(...) ne::Logger::log(ne::Logger::LogType::Error, __VA_ARGS__)
#define NE_FATAL(...) ne::Logger::log(ne::Logger::LogType::Fatal, __VA_ARGS__)
#else
// Stripped clean out of production builds
#define NE_LOG(...)
#define NE_WARN(...)
#define NE_ERROR(...)
#define NE_FATAL(...)
#endif // NE_BUILD_SHIPPING
