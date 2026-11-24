//------------------------------------------------------
// 文件名:async_logger.cpp
// 创建日期:2026-11-24
// 创建者:pppaisha
// 文件描述:异步日志器实现文件
//------------------------------------------------------
#include "utils/async_logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <iostream>

AsyncLogger::AsyncLogger(const std::string& filePath, LogLevel level)
    :m_file(filePath,std::ios::out | std::ios::app),m_level(level)
{
    if(!m_file.is_open())
    {
        throw std::runtime_error("Failed to open log file:" + filePath);
    }
    m_thread = std::thread(&AsyncLogger::threadFunc,this);
}

AsyncLogger::~AsyncLogger()
{
    stop();
}

void AsyncLogger::setLevel(LogLevel level) noexcept
{
    m_level.store(level);
}

const char* AsyncLogger::levelToString(LogLevel level)
{
    switch(level)
    {
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::WARN:
            return "WARN";
        case LogLevel::ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

std::string AsyncLogger::formatMessage(LogLevel level, const std::string& message)
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    if(::localtime_r(&time,&localTime) == nullptr)
    {
        throw std::runtime_error("localtime_r failed");
    }
    std::ostringstream oss;
    oss <<"["<< std::put_time(&localTime,"%Y-%m-%d %H:%M:%S")<<"]"
        <<"["<< levelToString(level)<<"] "
        << "[Thread "<< std::this_thread::get_id()<<"]"
        << message;
    return oss.str();
}

bool AsyncLogger::log(LogLevel level, const std::string& message)
{
    if(level < m_level.load(std::memory_order_relaxed))
    {return false;}
    std::string formatted = formatMessage(level,message);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if(m_stopping)
        {
            return false;
        }
        m_messages.push_back(std::move(formatted));
    }
    m_cv.notify_one();
    return true;
}

void AsyncLogger::threadFunc()
{
    std::vector<std::string> messages;
    while(true)
    {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock,[this]()
            {
                return m_stopping || !m_messages.empty();
            });
            if(m_stopping && m_messages.empty())
            {break;}
            messages.swap(m_messages);
        }
        for(const auto& message : messages)
        {
            m_file << message <<std::endl;
        }
        m_file.flush();
        messages.clear();
    }
    m_file.flush();
    if(!m_file)
    {
        std::cerr<<"AsyncLogger: log write failed"<<std::endl;
    }
}

void AsyncLogger::stop()
{
    std::lock_guard<std::mutex> lock(m_stopMutex);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
    }
    m_cv.notify_one();
    if(m_thread.joinable())
    {
        m_thread.join();
    }
    if(m_file.is_open())
    {
        m_file.close();
    }
}
