//------------------------------------------------------
// 文件名:multi_reactor_test.cpp
// 创建日期:2026-11-22
// 创建者:pppaisha
// 文件描述:多reactor测试文件
//------------------------------------------------------
#include "chat/chat_service.h"
#include "test_message_receiver.h"
#include "net/event_loop.h"
#include "net/tcp_server.h"
#include "net/length_field_codec.h"
#include "net/tcp_connection.h"

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <memory>
#include <future>
#include <cstdint>
#include <exception>
#include <thread>
#include <array>
#include <map>
#include <set>

//完整发送数据
void sendAll(int fd, const std::string& data)
{
    std::size_t offset = 0;
    while(offset < data.size())
    {
        ssize_t n = ::send(fd,data.data()+offset, data.size()-offset,MSG_NOSIGNAL);
        if(n>0)
        {
            offset += static_cast<std::size_t>(n);
        }
        else if(n<0 && errno == EINTR)
        {
            continue;
        }
        else
        {
            throw std::runtime_error("send failed");
        }
    }
}

//建立客户端连接
int connectClient(uint16_t port)
{
    int fd = ::socket(AF_INET,SOCK_STREAM,0);
    if(fd < 0)
    {
        throw std::runtime_error("socket failed");
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if(::connect(fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr)) != 0)
    {
        ::close(fd);
        throw std::runtime_error("connect failed");
    }
    return fd;
}

//消息编码
std::string encodeFrame(const std::string& message)
{
    uint32_t length = htonl(
        static_cast<uint32_t>(message.size())
    );

    return std::string(
        reinterpret_cast<const char*>(&length),
        sizeof(length)
    ) + message;
}

//发送聊天命令
void sendCommand(int fd, const std::string& command)
{
    sendAll(fd,encodeFrame(command));
}

//检查预期响应
void expectMessage(TestMessageReceiver& receiver, const std::string& expected)
{
    std::string actual = receiver.receiveMessage();
    if(actual != expected)
    {
        throw std::runtime_error(
            "Expected message: " + expected +
            ", but received: " + actual
        );
    }
}

//查询在线的客户端数量
std::size_t queryOnlineCount(EventLoop& loop, ChatService& chat)
{
    auto promise = std::make_shared<std::promise<std::size_t>>();
    auto future = promise->get_future();
    loop.queueInLoop([&chat,promise]()
    {
        promise->set_value(chat.onlineCount());
    });
    if(future.wait_for(std::chrono::seconds(3)) != std::future_status::ready)
    {
        throw std::runtime_error("Timeout waiting for online count");
    }
    return future.get();
}

void testMultipleFrames()
{
    LengthFieldCodec decoder;
    std::string data = encodeFrame("First") + encodeFrame("Second") + encodeFrame("Third");
    auto messages = decoder.feed(data.data(),data.size());

    if(messages.size() != 3 ||
       messages[0] != "First" ||
       messages[1] != "Second" ||
       messages[2] != "Third")
    {
        throw std::runtime_error("Multiple frames decoding failed");
    }
    std::cout<<"Multiple frames decoding passed!"<<std::endl;
}

using LoopMap = std::map<std::string,const EventLoop*>;

LoopMap queryLoopMap(EventLoop& loop, const LoopMap& connectionLoops)
{
    auto promise = std::make_shared<std::promise<LoopMap>>();
    auto future = promise->get_future();
    loop.queueInLoop([promise,&connectionLoops]()
    {
        promise->set_value(connectionLoops);
    });
    if(future.wait_for(std::chrono::seconds(3)) != std::future_status::ready)
    {
        throw std::runtime_error("Timeout waiting for loop map");
    }
    return future.get();
}

int main()
{
    testMultipleFrames();                   
    constexpr uint16_t port = 12082;
    EventLoop loop;//主EventLoop
    ChatService chat(loop);
    LoopMap connectionLoops;
    TcpServer server(loop,port,3);
    
    server.setMessageCallback([&loop,&chat,&connectionLoops](const std::shared_ptr<TcpConnection>& connection,
                                                            const std::string& message)
    {
        if(!loop.isInLoopThread())
        {
            throw std::logic_error("Message callback must be called in the loop thread");
        }
        if(message.rfind("LOGIN ",0) == 0)
        {
            std::string userId = message.substr(6);
            connectionLoops[userId] = &connection->getLoop();
        }
        chat.handleMessage(connection,message);
    });

    server.setConnectionCloseCallback([&chat](int fd)
    {
        chat.removeConnection(fd);
    });
    server.start();

    std::exception_ptr error;
    std::promise<void> shutdownPromise;
    auto shutdownFuture = shutdownPromise.get_future();
    std::thread worker([&]()
    {
        std::array<int,6> fds;
        fds.fill(-1);
        try
        {
            const std::array<std::string,6> users = 
            {
                "Alice","Bob","Charlie","David","Eve","Frank"
            };

            //Test1:六个客户端连接并登录
            for(std::size_t i=0; i<fds.size(); ++i)
            {
                fds[i] = connectClient(port);
                TestMessageReceiver receiver(fds[i]);
                sendCommand(fds[i],"LOGIN "+users[i]);
                expectMessage(receiver,"LOGIN_OK");
            }
            if(queryOnlineCount(loop,chat) != 6)
            {
                throw std::runtime_error("Expected six online users");
            }
            std::cout<<"Six clients login passed!"<<std::endl;

            //Test2: 验证Round-Robin分配
            auto loops = queryLoopMap(loop,connectionLoops);
            if(loops.size() != 6)
            {
                throw std::runtime_error("Expected six connection records");
            }
            std::set<const EventLoop*> uniqueLoops;
            for(const auto& [userId,ioLoop]:loops)
            {
                if(ioLoop == &loop)
                {
                    throw std::runtime_error("Connection assigned to base loop");
                }
                uniqueLoops.insert(ioLoop);
            }
            if(uniqueLoops.size() != 3 || loops.at("Alice") != loops.at("David") || loops.at("Bob") != loops.at("Eve") || loops.at("Charlie") != loops.at("Frank"))
            {
                throw std::runtime_error("Round-Robin distribution failed");
            }
            std::cout<<"Round-robin passed!"<<std::endl;

            TestMessageReceiver receiverA(fds[0]);
            TestMessageReceiver receiverB(fds[1]);
            TestMessageReceiver receiverC(fds[2]);
            TestMessageReceiver receiverD(fds[3]);
            TestMessageReceiver receiverE(fds[4]);
            TestMessageReceiver receiverF(fds[5]);

            //Test3:跨线程私聊
            sendCommand(fds[0],"SEND Bob Hello");
            expectMessage(receiverB,"FROM Alice Hello");
            expectMessage(receiverA,"SEND_OK");
            std::cout<<"Cross-thread private message passed!"<<std::endl;

            //Test4:跨线程广播
            sendCommand(fds[0],"BROADCAST Hi all");
            expectMessage(receiverB,"BROADCAST_FROM Alice Hi all");
            expectMessage(receiverC,"BROADCAST_FROM Alice Hi all");
            expectMessage(receiverD,"BROADCAST_FROM Alice Hi all");
            expectMessage(receiverE,"BROADCAST_FROM Alice Hi all");
            expectMessage(receiverF,"BROADCAST_FROM Alice Hi all");
            expectMessage(receiverA,"BROADCAST_OK");
            std::cout<<"Cross-thread broadcast passed!"<<std::endl;

            //Test5:向不存在的用户发送信息
            sendCommand(fds[0],"SEND Nobody Hello");
            expectMessage(receiverA,"SEND_FAILED");
            std::cout<<"Invalid receiver passed!"<<std::endl;

            //Test6:连续发送多条消息
            sendCommand(fds[0],"SEND Bob First");
            sendCommand(fds[0],"SEND Bob Second");
            sendCommand(fds[0],"SEND Bob Third");
            expectMessage(receiverB,"FROM Alice First");
            expectMessage(receiverB,"FROM Alice Second");
            expectMessage(receiverB,"FROM Alice Third");
            expectMessage(receiverA,"SEND_OK");
            expectMessage(receiverA,"SEND_OK");
            expectMessage(receiverA,"SEND_OK");
            std::cout<<"Multiple messages passed!"<<std::endl;
        }
        catch(...)
        {
            error = std::current_exception();
        }
        //Test7:保持客户端连接 主动停止服务器
        loop.queueInLoop([&chat,&server]()
        {
            chat.stop();
            server.stop();
        });
        //等待服务器关闭所有连接并退出事件循环
        //客户端暂不主动关闭 由主线程负责后续通知
        
        shutdownFuture.wait();
        for(int fd: fds)
        {
            if(fd >= 0)
            {
                ::close(fd);
            }
        }
    });
    loop.run();
    shutdownPromise.set_value();
    worker.join();
    if(error)
    {
        try
        {
            std::rethrow_exception(error);
        }
        catch(const std::exception& e)
        {
            std::cerr<<"Test faied:"<<e.what()<<std::endl;
        }
        return 1;
    }
    if(server.connectionCount() != 0)
    {
        std::cerr<<"Connections not cleared!"<<std::endl;
        return 1;
    }
    if(chat.onlineCount() != 0)
    {
        std::cerr<<"Sessions not cleared!"<<std::endl;
        return 1;
    }
    std::cout<<"Server shutdown passed!"<<std::endl;
    std::cout<<"Multi-Reactor test passed!"<<std::endl;
    return 0;
}