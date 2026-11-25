//------------------------------------------------------
// 文件名:async_logger_test.cpp
// 创建日期:2026-11-24
// 创建者:pppaisha
// 文件描述:异步日志器测试文件
//------------------------------------------------------
#include "utils/async_logger.h"

#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <stdexcept>
#include <atomic>

constexpr int THREAD_COUNT = 4;
constexpr int MESSAGE_COUNT = 1000;

const std::string LOG_PATH = "async_logger_test.log";

int main()
{
    try
    {
        //清空上一次测试产生的日志
        {
            std::ofstream file(LOG_PATH,std::ios::trunc);
            if(!file)
            {
                throw std::runtime_error("Failed to clear log file");
            }
        }
        AsyncLogger logger(LOG_PATH, LogLevel::INFO); //构造一个异步日志器对象，日志等级为INFO
        
        //Test1::日志等级过滤
        if(logger.log(LogLevel::DEBUG,"Fitered message") || !logger.log(LogLevel::INFO,"Info message")
             || !logger.log(LogLevel::ERROR,"Error message") || !logger.log(LogLevel::WARN,"Warn message"))
        {
            throw std::runtime_error("Log level filtering failed");
        }
        std::cout<<"[pass] Log level filtering"<<std::endl;

        //Test2:动态修改日志等级
        logger.setLevel(LogLevel::DEBUG);
        if(!logger.log(LogLevel::DEBUG,"Debug enabled"))
        {
            throw std::runtime_error("Dynamic log level failed");
        }
        std::cout<<"[pass] Dynamic log level"<<std::endl;

        //Test3:多个线程并发提交日志
        std::vector<std::thread> workers;
        std::atomic<int> accepted{0};
        for(int i=0; i<THREAD_COUNT; ++i)
        {
            //先创建了4个线程 让每个线程提交1000条日志 统计成功提交的日志数量
            workers.emplace_back([&logger,&accepted,i]()
            {
                for(int j=0; j<MESSAGE_COUNT; ++j)
                {
                    if(logger.log(LogLevel::INFO,"Worker " + std::to_string(i) + " message" + std::to_string(j)))
                    {
                        accepted.fetch_add(1,std::memory_order_relaxed);
                    }
                }
            });
        }
        for(auto& worker : workers)
        {
            worker.join();
        }
        if(accepted.load() != THREAD_COUNT*MESSAGE_COUNT)
        {
            throw std::runtime_error("Concurrent log submission failed");
        }
        std::cout<<"[pass] Concurrent log submission"<<std::endl;

        //Test4:安全停止和重复停止
        logger.stop();
        logger.stop();
        if(logger.log(LogLevel::INFO,"After stop"))
        {
            throw std::runtime_error("Logget accepted message after stop");
        }
        std::cout<<"[pass] Safe logger shutdown"<<std::endl;

        //Test5:验证日志文件
        std::ifstream file(LOG_PATH);
        if(!file)
        {
            throw std::runtime_error("Failed to read log file");
        }
        std::string line;
        int count = 0;
        bool debugEnabled = false;
        while(std::getline(file,line))
        {
            ++count;
            if(line.find("Filtered message") != std::string::npos || line.find("After stop") != std::string::npos)
            {
                throw std::runtime_error("Unexpected log message");
            }
            if(line.find("[DEBUG]") != std::string::npos && line.find("Debug enabled") != std::string::npos)
            {
                debugEnabled = true;
            }
        }
        const int expected = THREAD_COUNT * MESSAGE_COUNT + 4;
        if(count != expected || !debugEnabled)
        {
            throw std::runtime_error("Log file verification failed");
        }
        std::cout<<"[pass] Log file verification"<<std::endl;
        std::cout<<"Total log messages:"<<count<<std::endl;
        std::cout<<"All async logger tests passed!"<<std::endl;
        return 0;
    }
    catch(const std::exception& e)
    {
        std::cerr<<"[failed]"<<e.what()<<std::endl;
        return 1;
    }
}