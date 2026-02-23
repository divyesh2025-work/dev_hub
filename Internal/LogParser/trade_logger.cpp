#include "../Utils/shm_logger.hpp"
#include <fstream>
#include <csignal>
#include <thread>
#include <unordered_map>
#include <sstream>
#include <iomanip>
#include <vector>

// NSE Contract data structure
struct NSEContract
{
    uint32_t finInstrmId;
    uint32_t undrlygFinInstrmId;
    std::string finInstrmNm;
    std::string tckrSymb;
    uint64_t xpryDt;
    int32_t strkPric;
    std::string optnTp;
    std::string stockNm;
};

// Enhanced log reader with contract details
class EnhancedLogReader
{
private:
    ShmLogger *reader_logger;
    std::unordered_map<std::string, NSEContract> contractMap;
    std::ofstream detailedLogFile;

public:
    EnhancedLogReader(const std::string &shm_name, const std::string &output_file)
    {
        reader_logger = new ShmLogger(shm_name, false); // false = attach only
        detailedLogFile.open(output_file, std::ios::app);
        if (!detailedLogFile.is_open())
        {
            std::cerr << "Failed to open detailed log file: " << output_file << std::endl;
        }
        loadTodaysContracts();
        writeDetailedLogHeader();
    }
    ~EnhancedLogReader()
    {
        if (reader_logger)
        {
            delete reader_logger;
        }
        if (detailedLogFile.is_open())
        {
            detailedLogFile.close();
        }
    }
    // Get today's NSE contract filename
    std::string getTodaysFilename()
    {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::localtime(&time_t);
        std::stringstream ss;
        ss << "../config/NSE_FO_contract_"
           << std::setfill('0') << std::setw(2) << tm.tm_mday
           << std::setfill('0') << std::setw(2) << (tm.tm_mon + 1)
           << (tm.tm_year + 1900) << ".csv";
        return ss.str();
    }
    // Get contract by token (finInstrmId)
    NSEContract *getContractByToken(const std::string &token)
    {
        auto it = contractMap.find(token);
        return (it != contractMap.end()) ? &it->second : nullptr;
    }

    // Load NSE contract data
    bool loadTodaysContracts()
    {
        std::string filename = getTodaysFilename();
        std::ifstream file(filename);
        if (!file.is_open())
        {
            std::cerr << "Warning: Cannot open " << filename << std::endl;
            return false;
        }
        std::string line;
        // Skip header
        std::getline(file, line);
        while (std::getline(file, line))
        {
            std::vector<std::string> tokens;
            std::stringstream ss(line);
            std::string token;
            while (std::getline(ss, token, ','))
            {
                tokens.push_back(token);
            }
            if (tokens.size() >= 7)
            {
                NSEContract contract;
                contract.finInstrmId = std::stoul(tokens[0]);
                contract.undrlygFinInstrmId = std::stoul(tokens[1]);
                contract.finInstrmNm = tokens[2];
                contract.tckrSymb = tokens[3];
                contract.xpryDt = std::stoull(tokens[4]);
                contract.strkPric = std::stoul(tokens[5]);
                contract.optnTp = tokens[6];
                contract.stockNm = tokens[18];
                // Use finInstrmId as key (this matches your leg.token)
                contractMap[std::to_string(contract.finInstrmId)] = contract;
            }
        }
        file.close();
        std::cout << "Loaded " << contractMap.size() << " contracts from " << filename << std::endl;
        return true;
    }
    // Format timestamp
    std::string formatTimestamp(uint64_t timestamp_ns)
    {
        using namespace std::chrono;
        auto tp = system_clock::time_point(nanoseconds(timestamp_ns));
        std::time_t t = system_clock::to_time_t(tp);

        std::ostringstream oss;
        oss << std::put_time(std::localtime(&t), "%F %T");
        return oss.str();
    }
    // Write detailed log header
    void writeDetailedLogHeader()
    {
        if (detailedLogFile.is_open())
        {
            detailedLogFile << "LogTimestamp,Token,FinInstrmId,UndrlygFinInstrmId,FinInstrmNm,TckrSymb,XpryDt,StrkPric,OptnTp,StockNm,Side,StratKind,PortfolioId,OmsOrderId,ExchangeOrderId,LegId,FillQty,FillPrice,ExchangeTradeTime" << std::endl;
        }
    }
    // Parse your log entry and extract fields
    std::vector<std::string> parseLogText(const std::string &text)
    {
        std::vector<std::string> fields;
        std::stringstream ss(text);
        std::string field;
        while (std::getline(ss, field, ','))
        {
            // Trim whitespace
            field.erase(0, field.find_first_not_of(" \t"));
            field.erase(field.find_last_not_of(" \t") + 1);
            fields.push_back(field);
        }
        return fields;
    }
    // Process log entry and write detailed log
    void processLogEntry(const LogEntry &entry)
    {
        std::string timestamp = formatTimestamp(entry.timestamp_ns);
        std::string module = entry.module;
        // Parse the logged data
        auto fields = parseLogText(entry.text);
        if (fields.size() >= 10)
        { // Expecting: token, side, strat_kind, portfolio_id, oms_order_id, exchange_order_id, leg_id, fill_qty, fill_price
            std::string token = fields[0];
            std::string side = fields[1];
            std::string stratKind = fields[2];
            std::string portfolioId = fields[3];
            std::string omsOrderId = fields[4];
            std::string exchangeOrderId = fields[5];
            std::string legId = fields[6];
            std::string fillQty = fields[7];
            std::string fillPrice = fields[8];
            std::string exchangeTime = fields[9];
            // Look up contract details
            auto *contract = getContractByToken(token);
            // // Write detailed log
            if (detailedLogFile.is_open())
            {
                detailedLogFile << timestamp << ","
                                << token << ",";
                if (contract)
                {
                    detailedLogFile << contract->finInstrmId << ","
                                    << contract->undrlygFinInstrmId << ","
                                    << contract->finInstrmNm << ","
                                    << contract->tckrSymb << ","
                                    << contract->xpryDt << ","
                                    << contract->strkPric << ","
                                    << contract->optnTp << ","
                                    << contract->stockNm << ",";
                }
                else
                {
                    detailedLogFile << ",,,,,,"; // Empty contract fields
                }
                detailedLogFile << side << ","
                                << stratKind << ","
                                << portfolioId << ","
                                << omsOrderId << ","
                                << exchangeOrderId << ","
                                << legId << ","
                                << fillQty << ","
                                << fillPrice << ","
                                << exchangeTime << std::endl;
                detailedLogFile.flush();
            }
            // Also print to console (existing behavior)
            std::string consoleOutput = timestamp + "," + entry.text;
            if (contract)
            {
                consoleOutput += " [" + contract->tckrSymb + " " +
                                 std::to_string(contract->strkPric) + " " +
                                 contract->optnTp + "]";
            }
            std::cout << consoleOutput << std::endl;
        }
    }
    // Main reading loop
    void startReading()
    {
        auto *buffer = reader_logger->shm()->data();
        LogEntry entry;
        std::cout << "Starting enhanced log reader..." << std::endl;
        std::cout << "Reading from shared memory and writing detailed logs..." << std::endl;
        while (true)
        {
            if (buffer->pop(entry))
            {
                processLogEntry(entry);
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }
    void cleanup()
    {
        if (reader_logger)
        {
            reader_logger->shm()->unlink();
            std::cout << "Shared memory [" << reader_logger->shm()->name() << "] unlinked." << std::endl;
        }
    }
};

// Global pointer for cleanup
EnhancedLogReader *globalReader = nullptr;

// Cleanup handler
void cleanup(int)
{
    if (globalReader)
    {
        globalReader->cleanup();
    }
    exit(0);
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        std::cerr << "Usage: ./<exe_name> <shm_name> <detailed_log_file>" << std::endl;
        return 1;
    }
    std::string shm_name = argv[1];
    std::string detailed_log_file = argv[2];
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);
    // Create enhanced reader
    globalReader = new EnhancedLogReader(shm_name, detailed_log_file);
    // Start reading and processing
    globalReader->startReading();
    return 0;
}


// g++ reader1.cpp -o trade_logger