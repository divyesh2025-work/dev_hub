#pragma once
#include <string>
#include "../Include/core/Config.h"

class ConfigLoader
{
public:
    static Config loadFromFile(const std::string &filename);

private:
    static void trim(std::string &s);
    static bool parseBool(const std::string &val);
};
