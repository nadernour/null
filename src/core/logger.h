#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <utility>

namespace ne {

// Thread-safe. Verbose lines are dropped before they are formatted unless init() turned them on. Verbose and Log
// lines go to stdout and the rest to stderr, colored on a terminal; every line also goes to logs/null_<start time>.log
// next to the executable, and the 10 newest log files are kept.
class Logger {
public:
  enum class LogLevel : uint8_t { Verbose, Log, Warn, Error, Fatal };

  // Called once at startup, before other threads exist; iVerbose shows verbose lines for the whole run
  static void init(bool iVerbose);

  template <typename... Args> static void log(LogLevel iLevel, std::format_string<Args...> iFmt, Args&&... iArgs) {
    if (iLevel != LogLevel::Verbose || isVerbose()) {
      write(iLevel, std::format(iFmt, std::forward<Args>(iArgs)...));
    }
  }

private:
  static bool isVerbose();
  static void write(LogLevel iLevel, const std::string& iMessage);
};

} // namespace ne

#define NE_VERBOSE(...) ne::Logger::log(ne::Logger::LogLevel::Verbose, __VA_ARGS__)
#define NE_LOG(...) ne::Logger::log(ne::Logger::LogLevel::Log, __VA_ARGS__)
#define NE_WARN(...) ne::Logger::log(ne::Logger::LogLevel::Warn, __VA_ARGS__)
#define NE_ERROR(...) ne::Logger::log(ne::Logger::LogLevel::Error, __VA_ARGS__)
#define NE_FATAL(...) ne::Logger::log(ne::Logger::LogLevel::Fatal, __VA_ARGS__)
