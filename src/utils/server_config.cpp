//------------------------------------------------------
// 文件名:server_config.cpp
// 创建日期:2026-11-26
// 创建者:pppaisha
// 文件描述:服务器配置实现文件
//------------------------------------------------------
#include "utils/server_config.h"

#include <fstream>
#include <stdexcept>
#include <unordered_set>

namespace
{
    std::string trim(const std::string& text)
    {
        const auto first = text.find_first_not_of(" \t\r\n");
        if(first == std::string::npos)
        {
            return "";
        }
        const auto last = text.find_last_not_of(" \t\r\n");
        return text.substr(first, last - first + 1);
    }

    unsigned long long parseNumber(const std::string& value, const std::string& key, unsigned long long maximum)
    {
        if(value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        {
            throw std::runtime_error("Invalid number: " + key);
        }
        auto number = std::stoull(value);
        if(number > maximum)
        {
            throw std::runtime_error("Number out of range: " + key);
        }
        return number;
    }

    LogLevel parseLogLevel(const std::string& value)
    {
        if(value == "DEBUG")
        {return LogLevel::DEBUG;}
        if(value == "INFO")
        {return LogLevel::INFO;}
        if(value == "WARN")
        {return LogLevel::WARN;}
        if(value == "ERROR")
        {return LogLevel::ERROR;}
        throw std::runtime_error("Invalid log level: " + value);
    }
}

ServerConfig ServerConfig::load(const std::string& filePath)
{
    std::ifstream input(filePath);
    if(!input.is_open())
    {
        throw std::runtime_error("Failed to open config: "+ filePath);
    }
    ServerConfig config;
    std::unordered_set<std::string> keys;
    std::string line;
    while(std::getline(input,line))
    {
        line = trim(line);
        if(line.empty() || line.front() == '#')
        {continue;}
        const auto pos = line.find('=');
        if(pos == std::string::npos)
        {
            throw std::runtime_error("Invalid config line: " + line);
        }
        std::string key = trim(line.substr(0,pos));
        std::string value = trim(line.substr(pos+1));
        if(!keys.insert(key).second)
        {
            throw std::runtime_error("Duplicate config key: " + key);
        }
        if(key == "port")
        {
            config.port = static_cast<uint16_t>(parseNumber(value,key,65535));
        }
        else if(key == "io_threads")
        {
            config.ioThreads = static_cast<std::size_t>(parseNumber(value,key,64));
        }
        else if(key == "idle_timeout")
        {
            config.idleTimeout = std::chrono::seconds(parseNumber(value,key,86400));
        }
        else if(key == "log_level")
        {
            config.logLevel = parseLogLevel(value);
        }
        else if(key == "log_file")
        {
            config.logFile = value;
        }
        else
        {
            throw std::runtime_error("Unknown config key: " + key);
        }
    }

    if(input.bad())
    {
        throw std::runtime_error("Config read failed");
    }
    config.validate();
    return config;
}

void ServerConfig::validate() const
{
    if(port == 0)
    {
        throw std::invalid_argument("Invalid port");
    }
    if(ioThreads > 64)
    {
        throw std::invalid_argument("Too many I/O threads");
    }
    if(idleTimeout.count() <=0 || idleTimeout.count() > 86400)
    {
        throw std::invalid_argument("Invalid idle timeout");
    }
    if(logLevel < LogLevel::DEBUG || logLevel > LogLevel::ERROR)
    {
        throw std::invalid_argument("Invalid log level");
    }
    if(logFile.empty())
    {
        throw std::invalid_argument("Log file cannot be empty");
    }
}