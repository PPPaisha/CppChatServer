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

//打开日志文件 启动日志线程
AsyncLogger::AsyncLogger(const std::string& filePath, LogLevel level)
    :m_file(filePath,std::ios::out | std::ios::app),m_level(level)
{
    if(!m_file.is_open())
    {
        throw std::runtime_error("Failed to open log file:" + filePath);
    }
    m_thread = std::thread([this]()
    {
        this->threadFunc();
    });
}

//自动回收日志系统资源
AsyncLogger::~AsyncLogger()
{
    stop();
}

//动态修改日志过滤等级
void AsyncLogger::setLevel(LogLevel level) noexcept
{
    m_level.store(level);
}

//将枚举转换为字符串
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

//格式化一条完整的日志消息 包含时间戳 日志等级 线程ID 消息内容
std::string AsyncLogger::formatMessage(LogLevel level, const std::string& message)
{
    const auto now = std::chrono::system_clock::now();//获取当前时间
    const std::time_t time = std::chrono::system_clock::to_time_t(now);//转换为time_t类型
    std::tm localTime{};//创建一个tm结构体用于存储本地时间
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

//过滤日志并提交到共享队列
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

//等待 批量获取并写入日志
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
        m_file.flush();//请求将C++输出流中尚未输出的数据刷新到底层文件
        messages.clear();
    }
    m_file.flush();
    if(!m_file)
    {
        std::cerr<<"AsyncLogger: log write failed"<<std::endl;
    }
}

//停止接收日志 等待写入完成并回收资源
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
