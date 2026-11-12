//------------------------------------------------------
// 文件名:tcp_server_test.cpp
// 创建日期:2026-11-9
// 创建者:pppaisha
// 文件描述:tcpserver类测试文件
//------------------------------------------------------
#include "net/tcp_server.h"
#include "net/length_field_codec.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <atomic>
#include <cerrno>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <mutex>

std::mutex errorMutex;
void sendAll(int fd, const std::string& data)
{
    std::size_t sent = 0;
    while(sent < data.size())
    {
        ssize_t n = ::send(fd,data.data()+sent,data.size()-sent,MSG_NOSIGNAL);
        if(n > 0)
        {
            sent += static_cast<std::size_t>(n);
        }
        else if(n < 0 && errno == EINTR)
        {
            continue;
        }
        else
        {
            throw std::runtime_error("send failed");
        }
    }
}

void runClient(int port, const std::string& expected)
{
    Socket client;
    timeval timeout{};
    timeout.tv_sec = 5;
    if(::setsockopt(client.getFd(),SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))!=0)
    {
        throw std::runtime_error("setsockopt failed");
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if(::connect(client.getFd(),reinterpret_cast<sockaddr*>(&address),sizeof(address))!=0)
    {
        throw std::runtime_error("connect failed");
    }
    sendAll(client.getFd(),LengthFieldCodec::encode(expected));
    LengthFieldCodec decoder;
    char buffer[4096];

    while(true)
    {
        ssize_t n = ::recv(client.getFd(),buffer,sizeof(buffer),0);
        if(n > 0)
        {
            auto messages = decoder.feed(buffer,static_cast<std::size_t>(n));
            if(messages.empty())
            {
                if(messages.size()!=1 || messages[0] != expected)
                {
                    throw std::runtime_error("received unexpected message");
                }
                break;
            }
        }
        else if(n < 0 && errno == EINTR)
        {
            continue;
        }
        else
        {
            throw std::runtime_error("recv failed");
        }
    }
    ::shutdown(client.getFd(),SHUT_WR);
}

int main()
{
    EventLoop loop;
    TcpServer server(loop,22015);
    std::atomic<int> closedCount{0};
    server.setConnectionCloseCallback([&](int)
    {
        if (++closedCount == 3)
        {
            loop.stop();
        }
    });
    server.setMessageCallback([](const TcpServer::ConnectionPtr& connection, const std::string& message)
    {
        connection->sendMessage(message);
    });
    server.start();

    std::exception_ptr clientError;
    std::vector<std::thread> clients;
    const std::vector<std::string> messages = {"Hello","World","CppChat"};
    for(const auto& message : messages)
    {
        clients.emplace_back([&,message]()
    {
        try
        {
            runClient(22015,message);
        }
        catch(...)
        {
            {
                std::lock_guard<std::mutex> lock(errorMutex);
                if(!clientError)
                {
                    clientError = std::current_exception();
                }
            }
            loop.stop();
        }
    });
    }
    //此处还需要一个可靠的停止条件
    //TcpServer当前尚未对外提供连接关闭通知
    loop.run();
    for(auto& client : clients)
    {
        client.join();
    }
    if(clientError)
    {
        std::rethrow_exception(clientError);
    }
    if(server.connectionCount()!=0)
    {
        throw std::runtime_error("server connection count mismatch");
    }
    std::cout<<"TcpServer test passed!"<<std::endl;
}
