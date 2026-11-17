//------------------------------------------------------
// 文件名:event_loop_task_test.cpp
// 创建日期:2026-11-17
// 创建者:pppaisha
// 文件描述:异步任务处理和投递测试
//------------------------------------------------------
#include "net/event_loop.h"

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

int main()
{
    EventLoop loop;
    const std::thread::id loopThreadId = std::this_thread::get_id();

    // 测试1:同线程runInLoop应立即执行
    bool immediateExecuted = false;
    loop.runInLoop([&]()
                   { immediateExecuted = true; });
    if (!immediateExecuted)
    {
        throw std::runtime_error("runInLoop did not execute immediately in the same thread");
    }

    // 测试2:多个生产者线程并发投递任务
    constexpr int producerCount = 4;
    constexpr int tasksPerProducer = 100;

    // 400个普通任务+1个runInLoop跨线程任务+1个在任务内部再次提交的任务
    constexpr int totalTasks = producerCount * tasksPerProducer + 2;
    int executedCount = 0;
    bool wrongThread = false;
    auto recordExecution = [&]()
    {
        if (std::this_thread::get_id() != loopThreadId)
        {
            wrongThread = true;
        }
        ++executedCount;
        if (executedCount == totalTasks)
        {
            loop.stop();
        }
    };

    std::vector<std::thread> producers;
    for (int i = 0; i < producerCount; ++i)
    {
        producers.emplace_back([&, i]()
    {
        for(int j=0; j<tasksPerProducer; ++j)
        {
            loop.queueInLoop([&,i,j]()
        {
            //使用i j验证捕获值异常
            if(i<0 || j<0)
            {
                throw std::runtime_error("Invalid task index");
            }
            recordExecution();
            //第一个生产者的第一个任务 再次向EventLoop提交一个任务
            if(i==0 && j==0)
            {
                loop.queueInLoop([&]()
                {
                    recordExecution();
                });
            }
        });
        } 
    });
    }

    //测试3:从其他线程调用runInLoop
    std::thread extraProducer([&]()
    {
        loop.runInLoop([&]()
        {
            recordExecution();
        });
    });
    for(auto& producer:producers)
    {
        producer.join();
    }
    extraProducer.join();
    //任务已经入队 但EventLoop尚未运行
    if(executedCount != 0)
    {
        throw std::runtime_error("Tasks executed in wrong thread");
    }

    //测试4:启动事件循环 执行全部任务
    loop.run();
    if(wrongThread)
    {
        throw std::runtime_error("Tasks executed in wrong thread");
    }
    if(executedCount != totalTasks)
    {
        throw std::runtime_error("Not all tasks executed");
    }
    std::cout<<"Executed tasks: "<<executedCount<<std::endl;
    std::cout<<"EventLoop task test passed!"<<std::endl;
    return 0;
}