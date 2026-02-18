#pragma once
#include <atomic>
#include <string>
class StrategyOrderIDManager
{
public:
    // Get the singleton instance
    static StrategyOrderIDManager &instance()
    {
        static StrategyOrderIDManager instance;
        return instance;
    }

    // Set the OMS ID (next generated will be value + 1)
    uint64_t set(uint32_t value)
    {
        counter.store(value);
        LOG_FILE("helper", "OMS ID set to: " + std::to_string(counter.load()));
        return counter.load();
    }

    // Get the current OMS ID
    uint64_t get() const
    {
        return counter.load();
    }

    // Generate a new unique OMS ID
    uint64_t generate()
    {
        LOG_FILE("helper", "Current OMS ID before generate: " + std::to_string(counter.load()));
        return counter.fetch_add(1);
    }

private:
    StrategyOrderIDManager() = default;
    std::atomic<uint32_t> counter{1};
};