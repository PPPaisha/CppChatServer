//------------------------------------------------------
// 文件名:event_loop_thread_pool_test.cpp
// 创建日期:2026-11-20
// 创建者:pppaisha
// 文件描述:事件循环线程池测试文件
//------------------------------------------------------
#include "net/event_loop.h"
#include "net/event_loop_thread_pool.h"

#include <cassert>
#include <future>
#include <iostream>
#include <set>
#include <thread>
#include <vector>

int main()
{
    EventLoop baseLoop;
    EventLoopThreadPool pool(baseLoop);
    pool.start(3);
    std::vector<EventLoop*> loops;
    for(int i=0; i<6; ++i)
    {
        loops.push_back(pool.getNextLoop());
    }
    //验证轮询顺序
    assert(loops[0] == loops[3]);
    assert(loops[1] == loops[4]);
    assert(loops[2] == loops[5]);
    assert(loops[0] != loops[1]);
    assert(loops[1] != loops[2]);
    assert(loops[0] != loops[2]);
    std::cout<<"Round-robin passed!"<<std::endl;

    //验证三个EventLoop在不同线程中执行
    std::vector<std::future<std::thread::id>> futures;
    for(int i=0; i<3; ++i)
    {
        auto promise = std::make_shared<std::promise<std::thread::id>>();
        futures.push_back(promise->get_future());
        loops[i]->queueInLoop([promise]()
        {
            promise->set_value(std::this_thread::get_id());
        });
    }
    std::set<std::thread::id> threadIds;
    for(auto& future:futures)
    {
        threadIds.insert(future.get());
    }

    assert(threadIds.size() == 3);
    assert(threadIds.find(std::this_thread::get_id()) == 0);
    std::cout<<"Thread ownership passed!"<<std::endl;

    //验证跨线程调度
    std::vector<std::future<int>> taskFutures;
    for(int i=0; i<30; ++i)
    {
        auto promise = std::make_shared<std::promise<int>>();
        taskFutures.push_back(promise->get_future());
        pool.getNextLoop()->queueInLoop([i,promise]()
        {
            promise->set_value(i*i);
        });
    }
    for(int i=0; i<30; ++i)
    {
        assert(taskFutures[i].get() == i*i);
    }
    std::cout<<"Cross-thread tasks passed!"<<std::endl;
    pool.stop();
    std::cout<<"Version2.3 test passed!"<<std::endl;
    return 0;
}