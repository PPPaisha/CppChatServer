//------------------------------------------------------
// 文件名:server_config.h
// 创建日期:2026-11-26
// 创建者:pppaisha
// 文件描述:服务器配置头文件
//------------------------------------------------------
#pragma once

#include "utils/async_logger.h"

#include <string>
#include <cstdint>
#include <cstddef>
#include <chrono>

struct ServerConfig
{
    uint16_t port = 12088;
    std::size_t ioThreads = 3;
    std::chrono::seconds idleTimeout {30};
    LogLevel logLevel = LogLevel::INFO;
    std::string logFile = "server.log";
    static ServerConfig load(const std::string& filePath);
    void validate() const; //负责检查配置参数是否合法
};



