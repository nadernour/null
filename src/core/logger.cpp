#include "core/logger.h"
#include "core/platform.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <vector>

#if defined(NE_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ne {

namespace {

constexpr size_t kKeptLogFiles = 10;
constexpr const char* kLogFilePrefix = "null_";
constexpr const char* kResetColor = "\x1b[0m";

struct LogLevelStyle {
  const char* name;
  const char* color; // ANSI escape code
};

// Indexed by LogLevel
constexpr LogLevelStyle kLogLevelStyles[] = {
    {"verbose", "\x1b[90m"}, // Gray
    {"log", "\x1b[32m"},     // Green
    {"warn", "\x1b[33m"},    // Yellow
    {"error", "\x1b[31m"},   // Red
    {"fatal", "\x1b[1;31m"}, // Bold red
};
static_assert(std::size(kLogLevelStyles) == static_cast<size_t>(Logger::LogLevel::Fatal) + 1);

std::chrono::local_seconds localTimeNow(const std::chrono::sys_info& iZone) {
  const std::chrono::sys_seconds now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
  return std::chrono::local_seconds{(now + iZone.offset).time_since_epoch()};
}

// Colors only on a terminal; a Windows console must also be switched to accept ANSI codes
bool supportsColor(bool iStderr) {
#if defined(NE_PLATFORM_WINDOWS)
  HANDLE console = GetStdHandle(iStderr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
  DWORD mode = 0;
  return GetConsoleMode(console, &mode) && SetConsoleMode(console, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#else
  return isatty(iStderr ? STDERR_FILENO : STDOUT_FILENO);
#endif
}

std::ofstream openLogFile(const std::chrono::sys_info& iZone) {
  const std::filesystem::path logDir = std::filesystem::path(platform::getExecutableDirectory()) / "logs";
  std::error_code ec;
  std::filesystem::create_directories(logDir, ec);

  std::vector<std::filesystem::path> logFiles;
  for (std::filesystem::directory_iterator it(logDir, ec), end; !ec && it != end; it.increment(ec)) {
    if (it->path().filename().string().starts_with(kLogFilePrefix)) {
      logFiles.push_back(it->path());
    }
  }
  std::sort(logFiles.begin(), logFiles.end());
  for (size_t i = 0; i + kKeptLogFiles <= logFiles.size(); ++i) { // Leaves room for the new file
    std::filesystem::remove(logFiles[i], ec);
  }

  const std::chrono::local_seconds start = localTimeNow(iZone);
  std::ofstream file(logDir / std::format("{}{:%Y-%m-%d_%H-%M-%S}.log", kLogFilePrefix, start), std::ios::app);
  if (!file) {
    std::cerr << "Logger: could not open a log file; logging to the console only\n";
  }
  file << std::format("Log started {:%F %T %Ez (%Z)}\n", std::chrono::local_time_format(start, &iZone.abbrev, &iZone.offset));
  return file;
}

struct LogState {
  bool verbose = false; // Only written by init(), before other threads exist
  std::chrono::sys_info zone = std::chrono::current_zone()->get_info(std::chrono::system_clock::now());
  std::mutex mutex;
  std::ofstream file = openLogFile(zone); // Writes do nothing if it failed to open
  bool colorStdout = supportsColor(false);
  bool colorStderr = supportsColor(true);
};

// Never destroyed, so code running during static destruction can still log
LogState& getLogState() {
  static LogState* state = new LogState();
  return *state;
}

} // namespace

void Logger::init(bool iVerbose) { getLogState().verbose = iVerbose; }

// Every line is flushed, so nothing is lost on a crash or abort()
void Logger::write(LogLevel iLevel, const std::string& iMessage) {
  LogState& state = getLogState();
  const LogLevelStyle& style = kLogLevelStyles[static_cast<size_t>(iLevel)];
  const std::chrono::local_seconds now = localTimeNow(state.zone);
  const bool toConsole = iLevel != LogLevel::Verbose || state.verbose;
  const bool toStdout = iLevel <= LogLevel::Log;
  std::string consoleLine;
  if (toConsole) {
    const bool useColor = toStdout ? state.colorStdout : state.colorStderr;
    consoleLine =
        std::format("[{:%T}] [{}{}{}] {}\n", now, useColor ? style.color : "", style.name, useColor ? kResetColor : "", iMessage);
  }
  const std::string fileLine = std::format("[{:%T}] [{}] {}\n", now, style.name, iMessage);

  std::lock_guard lock(state.mutex);
  if (toConsole) {
    std::ostream& console = toStdout ? std::cout : std::cerr;
    console << consoleLine << std::flush;
  }
  state.file << fileLine << std::flush;
}

} // namespace ne
