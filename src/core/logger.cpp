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

// Wall-clock time in the machine's time zone, or UTC when none is set
std::chrono::local_seconds localTimeNow() {
  return std::chrono::floor<std::chrono::seconds>(std::chrono::current_zone()->to_local(std::chrono::system_clock::now()));
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

// File names start with the start time, so name order is age order. Files without the prefix are left alone.
std::ofstream openLogFile() {
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

  std::ofstream file(logDir / std::format("{}{:%Y-%m-%d_%H-%M-%S}.log", kLogFilePrefix, localTimeNow()), std::ios::app);
  if (!file) {
    std::cerr << "Logger: could not open a log file; logging to the console only\n";
  }
  return file;
}

struct LogState {
  bool verbose = false; // Only written by init(), before other threads exist
  std::mutex mutex;
  std::ofstream file = openLogFile(); // Writes do nothing if it failed to open
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

bool Logger::isVerbose() { return getLogState().verbose; }

// Every line is flushed, so nothing is lost on a crash or abort()
void Logger::write(LogLevel iLevel, const std::string& iMessage) {
  LogState& state = getLogState();
  const LogLevelStyle& style = kLogLevelStyles[static_cast<size_t>(iLevel)];
  const std::chrono::local_seconds now = localTimeNow();
  const bool toStdout = iLevel <= LogLevel::Log;
  const bool useColor = toStdout ? state.colorStdout : state.colorStderr;
  const std::string consoleLine =
      std::format("[{:%T}] [{}{}{}] {}\n", now, useColor ? style.color : "", style.name, useColor ? kResetColor : "", iMessage);
  const std::string fileLine = std::format("[{:%T}] [{}] {}\n", now, style.name, iMessage);

  std::lock_guard lock(state.mutex);
  std::ostream& console = toStdout ? std::cout : std::cerr;
  console << consoleLine << std::flush;
  state.file << fileLine << std::flush;
}

} // namespace ne
