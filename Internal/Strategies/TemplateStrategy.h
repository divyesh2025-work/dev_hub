#pragma once

#include <algorithm>
#include "Utils/log_shm.hpp"
#if defined(__GNUC__) || defined(__clang__)
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#else
#define ALWAYS_INLINE inline
#endif

template <StrategyKind Kind>
struct StrategyExecutor;
using Side = OrderSide;
