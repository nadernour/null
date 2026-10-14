#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <utility>

namespace ne {

class Logger {
public:
  enum class LogLevel : uint8_t { Verbose, Log, Warn, Error, Fatal };

  static void init(bool iVerbose);

  template <typename... Args> static void log(LogLevel iLevel, std::format_string<Args...> iFmt, Args&&... iArgs) {
    write(iLevel, std::format(iFmt, std::forward<Args>(iArgs)...));
  }

private:
  static void write(LogLevel iLevel, const std::string& iMessage);
};

} // namespace ne

#define NE_VERBOSE(...) ne::Logger::log(ne::Logger::LogLevel::Verbose, __VA_ARGS__)
#define NE_LOG(...) ne::Logger::log(ne::Logger::LogLevel::Log, __VA_ARGS__)
#define NE_WARN(...) ne::Logger::log(ne::Logger::LogLevel::Warn, __VA_ARGS__)
#define NE_ERROR(...) ne::Logger::log(ne::Logger::LogLevel::Error, __VA_ARGS__)
#define NE_FATAL(...) ne::Logger::log(ne::Logger::LogLevel::Fatal, __VA_ARGS__)
