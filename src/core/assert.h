#pragma once

#include "core/defines.h"
#include "logger.h"
#include <cstdlib>

#ifndef NE_BUILD_SHIPPING
#define NE_ASSERT(condition, ...)                                                                                                \
  do {                                                                                                                           \
    if (!(condition)) {                                                                                                          \
      std::string user_msg;                                                                                                      \
      __VA_OPT__(user_msg = std::format(__VA_ARGS__);)                                                                           \
      ne::Logger::log(ne::Logger::LogLevel::Fatal, "ASSERTION FAILED: {}\n  Condition: {}\n  File: {}\n  Line: {}", user_msg,    \
                      #condition, __FILE__, __LINE__);                                                                           \
      NE_BREAK();                                                                                                                \
      std::abort();                                                                                                              \
    }                                                                                                                            \
  } while (false)

#else
#define NE_ASSERT(condition, ...)                                                                                                \
  do {                                                                                                                           \
    (void)sizeof(condition); /* Marks the operands used without evaluating them */                                               \
  } while (false)
#endif
