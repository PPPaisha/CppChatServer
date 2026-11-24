//------------------------------------------------------
// 文件名:async_logger.h
// 创建日期:2026-11-24
// 创建者:pppaisha
// 文件描述:异步日志器头文件
//------------------------------------------------------
#pragma once

#include <string>
#include <fstream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <atomic>

enum class LogLevel
{
    DEBUG,//调试信息 消息处理细节
    INFO,//服务器启动 客户端连接
    WARN,//空闲超时 发送缓冲区达到上限
    ERROR//系统调用失败 严重运行错误
};

class AsyncLogger
{
public:
    explicit AsyncLogger(const std::string& filePath, LogLevel level = LogLevel::INFO);
    ~AsyncLogger();
    AsyncLogger(const AsyncLogger&) = delete;
    AsyncLogger& operator=(const AsyncLogger&) = delete;
    
    bool log(LogLevel level, const std::string& message);
    void setLevel(LogLevel level) noexcept;
    void stop();

private:
    void threadFunc();
    static const char* levelToString(LogLevel level);
    static std::string formatMessage(LogLevel level, const std::string& message);

private:
    std::ofstream m_file; //持有日志文件输出流
    std::thread m_thread; //专门负责日志写入的线程
    std::mutex m_mutex; //保护日志队列和停止状态
    std::condition_variable m_cv; //唤醒等待日志的线程
    std::vector<std::string> m_messages; //保存尚未写入文件的日志
    std::atomic<LogLevel> m_level; //当前允许输出的最低日志等级
    bool m_stopping = false; //是否已经开始停止日志系统
    std::mutex m_stopMutex; //防止多个线程同时执行join
};
