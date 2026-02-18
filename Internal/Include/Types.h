#pragma once

// Core headers
#include "core/Macros.h"
#include "core/BasicTypes.h"
#include "core/Config.h"

// Market data headers
#include "market/MarketData.h"
#include "market/MarketSnapshot.h"

// Order headers
#include "orders/OrderEnums.h"
#include "orders/OrderStructs.h"
#include "orders/OrderTracking.h"

// Frontend headers
#include "frontend/FrontendEnums.h"
#include "frontend/FrontendMessages.h"

// Strategy headers
#include "strategy/StrategyEnums.h"
#include "strategy/StrategyParams.h"
#include "strategy/StrategyOrderData.h"
#include "strategy/Portfolio.h"

// Standard library includes (if needed globally)
#include <cstdint>
#include <cstring>
#include <atomic>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

// Include Logger if needed
// #include "Logger.h"