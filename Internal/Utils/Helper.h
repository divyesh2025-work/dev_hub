#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include "Library/flat_hash_map.hpp"
#include <Include/core/Macros.h>
struct InstrumentData
{
    uint32_t min_lot;
    uint32_t max_lot;
    uint32_t bid_interval;
};
namespace helper
{

    inline std::string error_to_msg_str(uint32_t error_code)
    {
        static const std::unordered_map<uint32_t, std::string> error_to_msg = {
            {0, "Success"},
            {1, "Invalid input"},
            {2, "Connection failed"},
            {3, "Timeout occurred"},
            {4, "Permission denied"},
            {5, "Unknown error"}};

        auto it = error_to_msg.find(error_code);
        if (it != error_to_msg.end())
        {
            return it->second;
        }
        else
        {
            return "Unrecognized error code: " + std::to_string(error_code);
        }
    }

    inline ska::flat_hash_map<uint32_t, InstrumentData> token_to_instrument_data;
    inline std::once_flag loadFlag;

    inline void load_nse_fo_contract_file(const std::string &filename = "instruments.csv")
    {

        std::call_once(loadFlag, [&]()
                       {
                           std::ifstream file(filename);
                           if (!file.is_open())
                           {
                               std::cerr << "Error opening file: " << filename << "\n";
                               return;
                           }

                           std::string line;
                           bool headerSkipped = false;

                           while (std::getline(file, line))
                           {
                               if (!headerSkipped)
                               {
                                   headerSkipped = true; // skip first line (header)
                                   continue;
                               }

                               std::stringstream ss(line);
                               std::string cell;
                               uint32_t finInstrmId = 0;
                               uint32_t minLot = 0;
                               uint32_t bidInterval = 0;
                               uint32_t maxQty = 0;

                               int colIndex = 0;

                               bool is_future = false;

                               while (std::getline(ss, cell, ','))
                               {
                                   if (colIndex == 0)
                                       finInstrmId = std::stoul(cell); // FinInstrmId
                                   if (colIndex == 5)
                                   {
                                       if (std::stoul(cell) == -1)
                                       {
                                           is_future = true;
                                       }
                                   }

                                   if (colIndex == 8)
                                       minLot = std::stoul(cell); // MinLot
                                   if (colIndex == 10)
                                       bidInterval = std::stoul(cell); // bidInteval

                                   if (colIndex == 40 && !cell.empty())
                                   {
                                       try
                                       {
                                           double parsed = std::stod(cell);
                                           maxQty = static_cast<uint32_t>(parsed);
                                       }
                                       catch (const std::invalid_argument &e)
                                       {
                                           std::cerr << "Invalid number format in cell: " << cell << "\n";
                                       }
                                       catch (const std::out_of_range &e)
                                       {
                                           std::cerr << "Number out of range in cell: " << cell << "\n";
                                       }
                                   }
                                   colIndex++;
                               }

                               if (is_future && finInstrmId > 0)
                               {
                                   token_to_instrument_data[finInstrmId] = InstrumentData{minLot, maxQty, bidInterval};
                               }
                           }

                           file.close(); });
    }

    inline uint32_t get_bid_interval(uint32_t token)
    {
        auto it = token_to_instrument_data.find(token);
        if (it != token_to_instrument_data.end())
        {
            return it->second.bid_interval;
        }
        else
        {
            LOG_COUT("This token not found in token to token_to_instrument_data map :" << token);
            LOG_FILE("helper", "This token not found intoken_to_instrument_data map :" + std::to_string(token));
        }
        return 0; // return 0 if token not found
    }

} // namespace utils
