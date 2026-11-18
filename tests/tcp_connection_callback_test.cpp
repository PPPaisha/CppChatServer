//------------------------------------------------------
// 文件名:tcp_conneciton_callback_test.cpp
// 创建日期:2026-11-18
// 创建者:pppaisha
// 文件描述:回调函数测试
//------------------------------------------------------
#include "net/tcp_connection.h"
#include "net/socket.h"
#include "net/event_loop.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

int main()
{
    EventLoop loop;
    Socket listener;
    listener.setReuseAddr(true);
    listener.setNonBlocking();
    listener.bind(0);
    listener.listen();

    sockaddr_in address{};
    socklen_t length = sizeof(address);

    if(::getsockname(listener.getFd(),reinterpret_cast<sockaddr*>(&address),&length) != 0)
    {
        throw std::runtime_error("getsockname failed");
    }
    const int port = ntohs(address.sin_port);

    //创建一条真实的TCP连接
    Socket client;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if(::connect(client.getFd(),reinterpret_cast<sockaddr*>(&address),sizeof(address)) != 0)
    {
        throw std::runtime_error("connect failed");
    }
    auto acceptedSocket = listener.accept();
    if(!acceptedSocket)
    {
        throw std::runtime_error("accept failed");
    }
    auto connection = std::make_shared<TcpConnection>(loop,std::move(*acceptedSocket));
    connection->start();

    auto successPromise = std::make_shared<std::promise<bool>>();
    auto failurePromise = std::make_shared<std::promise<bool>>();
    auto successFuture = successPromise->get_future();
    auto failureFuture = failurePromise->get_future();

    //测试1:正常发送
    connection->sendMessageAsync("Hello",[successPromise](bool accepted)
    {
        successPromise->set_value(accepted);
    });

    //测试2:销毁连接后执行待处理任务
    auto temporaryConnection = std::make_shared<TcpConnection>(loop,Socket{});
    temporaryConnection->sendMessageAsync("Will not send",[failurePromise](bool accepted)
    {
        failurePromise->set_value(accepted);
    });
    temporaryConnection.reset();

    //最后提交停止任务
    loop.queueInLoop([&]()
    {
        loop.stop();
    });
    loop.run();

    const bool success = successFuture.get();
    const bool failure = failureFuture.get();
    if(!success)
    {
        throw std::runtime_error("Nomal send callback failed");
    }
    if(failure)
    {
        throw std::runtime_error("Destroyed connection callback incorrect");
    }
    std::cout<<"Normal send callback : true"<<std::endl;
    std::cout<<"Destroyed connection callback : false"<<std::endl;
    std::cout<<"TcpConnection callback test passed!"<<std::endl;
    return 0;
}