// logger.h
#pragma once
#include <fstream>
#include <iostream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <vector>
class Logger
{
public:
    std::unordered_map<std::string, std::string> moduleColors;

    std::string getColorForModule(const std::string &module)
    {
        // Known modules get fixed colors
        if (moduleColors.find(module) != moduleColors.end())
        {
            return moduleColors[module];
        }

        // Dynamically assign new color
        static const std::vector<std::string> colorCodes = {
            "\033[31m", // red
            "\033[32m", // green
            "\033[33m", // yellow
            "\033[34m", // blue
            "\033[35m", // magenta
            "\033[36m", // cyan
            "\033[91m", // light red
            "\033[92m", // light green
            "\033[93m", // light yellow
            "\033[94m", // light blue
            "\033[95m", // light magenta
            "\033[96m", // light cyan
        };

        std::string color = colorCodes[moduleColors.size() % colorCodes.size()];
        moduleColors[module] = color;
        return color;
    }

    static Logger &instance()
    {
        static Logger inst;
        return inst;
    }

    // Helper function to get current timestamp
    std::string getCurrentTimestamp()
    {
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);

        std::stringstream ss;
        ss << std::put_time(std::localtime(&now_time), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    void logToFile(const std::string &msg)
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (file.is_open())
        {
            file << "[" << getCurrentTimestamp() << "] " << msg << std::endl;
        }
    }

    void logToCout(const std::string &msg)
    {
        std::lock_guard<std::mutex> lock(mtx);
        std::cout << "[" << getCurrentTimestamp() << "] " << msg << std::endl;
    }

    void logToFile(const std::string &module, const std::string &msg)
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (file.is_open())
        {
            file << "[" << getCurrentTimestamp() << "] "
                 << "[" << module << "] " << msg << std::endl;
        }
    }

    void logToCout(const std::string &module, const std::string &msg)
    {
        std::lock_guard<std::mutex> lock(mtx);
        std::string color = getColorForModule(module);
        std::string reset = "\033[0m";

        std::cout << "[" << getCurrentTimestamp() << "] "
                  << color << "[" << module << "]" << reset << " "
                  << msg << std::endl;
    }

    // void logToFile(const std::string& msg) {
    //     std::lock_guard<std::mutex> lock(mtx);
    //     if (file.is_open())
    //         file << msg << std::endl;
    // }

    // void logToCout(const std::string& msg) {
    //     std::lock_guard<std::mutex> lock(mtx);
    //     std::cout << msg << std::endl;
    // }

private:
    Logger()
    {
        // Get current time
        auto t = std::time(nullptr);
        auto tm = *std::localtime(&t);

        // Format: log_neo_feature_engine_YYYYMMDD_HHMMSS.txt
        std::ostringstream oss;
        oss << "log_neo_feature_engine_"
            << std::put_time(&tm, "%Y%m%d_%H%M%S")
            << ".txt";

        std::string filename = oss.str();
        file.open(filename, std::ios::out | std::ios::app);
        if (!file)
        {
            std::cerr << "Failed to open log file: " << filename << std::endl;
        }
        else
        {
            std::cout << "Logging to: " << filename << std::endl;
        }
    }

    ~Logger()
    {
        if (file.is_open())
            file.close();
    }

    std::ofstream file;
    std::mutex mtx;
};
