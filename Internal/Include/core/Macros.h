#pragma once
#include "../../Utils/Logger.h"
#ifdef _MSC_VER
#define ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#else
#define ALWAYS_INLINE inline
#endif

#define ENABLE_LOGGING 0
#if ENABLE_LOGGING
#define LOG_TEST(x) std::cout << x << std::endl
#else
#define LOG_TEST(x)
#endif

#define ENABLE_LOGGING_COUT 1 //
#if ENABLE_LOGGING_COUT
#define LOG_COUT(x) std::cout << x << std::endl
#else
#define LOG_COUT(x)
#endif

#define ENABLE_LOGGING_COUT_1 0
#if ENABLE_LOGGING_COUT_1
#define LOG_COUT_1(module, msg) Logger::instance().logToFile(module, msg)
#else
#define LOG_COUT_1(module, msg)
#endif

#define ENABLE_LOGGING_FILE 1 //
#if ENABLE_LOGGING_FILE
#define LOG_FILE(module, msg) Logger::instance().logToFile(module, msg)
#else
#define LOG_FILE(module, msg)
#endif

#define ENABLE_LOGGING_LIVE 1
#if ENABLE_LOGGING_LIVE
#define LOG_LIVE(module, msg) Logger::instance().logToFile(module, msg)
#else
#define LOG_LIVE(module, msg)
#endif