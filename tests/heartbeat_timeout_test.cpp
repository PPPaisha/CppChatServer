//------------------------------------------------------
// 文件名:heartbeat_timeout_test.cpp
// 创建日期:2026-11-20
// 创建者:pppaisha
// 文件描述:心跳超时测试文件
//------------------------------------------------------
#include "net/event_loop.h"
#include "net/tcp_server.h"
#include "net/length_field_codec.h"
#include "chat/chat_service.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <exception>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

constexpr uint16_t PORT = 12085;
constexpr int TIMEOUT_MS = 3000; //3秒超时

//将完整数据发送到Socket
void sendAll(int fd, const std::string& data)
{
    std::size_t sent = 0;
    while(sent < data.size())
    {
        ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if(n > 0)
        {
            sent += static_cast<std::size_t>(n);
            continue;
        }
        if(n < 0 && errno == EINTR)
        {
            continue;
        }
        throw std::runtime_error("send failed");
    }
}

//将消息编码成长度字段协议
std::string encodeFrame(const std::string& message)
{
    return LengthFieldCodec::encode(message);
}

void sendCommand(int fd, const std::string& message)
{
    sendAll(fd, encodeFrame(message));
}

//创建一个连接到本机服务器的测试客户端
int connectClient(uint16_t port)
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0)
    {
        throw std::runtime_error("socket failed");
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);

    if(::connect(fd, reinterpret_cast<sockaddr*>(&address),sizeof(address)) != 0)
    {
        ::close(fd);
        throw std::runtime_error("connect failed");
    }
    return fd;
}

//RAII管理客户端Socket
class ClientSocket
{
public:
    explicit ClientSocket(int fd):m_fd(fd)
    {}
    ~ClientSocket()
    {
        if(m_fd >= 0)
        {
            ::close(m_fd);
        }
    }
    ClientSocket(const ClientSocket&) = delete;
    ClientSocket& operator=(const ClientSocket&) = delete;
    int getFd() const noexcept
    {
        return m_fd;
    }
private:
    int m_fd;
};

//持续保存解码状态 防止TCP拆包 粘包
class TetsMessageReceiver
{
public:
    explicit TestMessageReceiver(int fd):m_fd(fd)
    {}
    std::string receiveMessage(int timeoutMs = TIMEOUT_MS)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while(m_messages.empty())
        {
            const auto now = std::chrono::steady_clock::now();
            if(now >= deadline)
            {
                throw std::runtime_error("Timeout waiting for message");
            }
            int remaining = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
            pollfd pfd{};
            pfd.fd = m_fd;
            pfd.events = POLLIN;
            int result = ::poll(&pfd, 1, remaining);
            if(result < 0)
            {
                if(errno == EINTR)
                {
                    continue;
                }
                throw std::runtime_error("poll failed");
            }
            char buffer[4096];
            ssize_t n = ::recv(m_fd, buffer, sizeof(buffer),0);
            if(n == 0)
            {
                throw std::runtime_error("Connection closed unexpectedly");
            }
            if(n < 0)
            {
                if(errno == EINTR || errno == EAGAIN)
                {
                    continue;
                }
                throw std::runtime_error("recv failed");
            }
            auto messages = m_decoder.feed(buffer,static_cast<std::size_t>(n));
            for(auto& message : messages)
            {
                m_messages.push_back(std::move(message));
            }
        }
        std::string message = std::move(m_messages.front());
        m_messages.erase(m_messages.begin());
        return message;
    }

private:
    int m_fd;
    LengthFieldCodec m_decoder;
    std::vector<std::string> m_messages;
};

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

//将查询任务提交到主EventLoop线程 安全读取在线人数
std::size_t queryOnlineCount(EventLoop& loop, ChatService& chat)
{
    auto promise = std::make_shared<std::promise<std::size_t>>();
    auto future = promise->get_future();
    loop.queueInLoop([&chat,promise]()
    {
        try
        {
            promise->set_value(chat.onlineCount());
        }
        catch(...)
        {
            promise->set_exception(std::current_exception());
        }
    });
    if(future.wait_for(std::chrono::seconds(3)) != std::future_status::ready)
    {
        throw std::runtime_error("Timeout waiting for online count");
    }
    return future.get();
}

//等待在线人数达到指定值
void waitForOnlineCount(EventLoop& loop, ChatService& chat, std::size_t expected, std::chrono::milliseconds timeout)
{
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while(std::chrono::steady_clock::now() < deadline)
    {
        if(queryOnlineCount(loop, chat) == expected)
        {return;}
        std::this_thread::sleep_for(50ms);
    }
    throw std::runtime_error("Online user count did not reach expected value");
}

//检查客户端是否被服务器关闭
bool waitForDisconnect(int fd, int timeoutMs)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while(std::chrono::steady_clock::now() < deadline)
    {
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;
        int result = ::poll(&pfd, 1, 100);
        if(result == 0)
        {
            continue;
        }
        if(result < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            throw std::runtime_error("poll failed");
        }
        char buffer[1024];
        ssize_t n = ::recv(fd,buffer,sizeof(buffer),MSG_DONTWAIT);
        if(n == 0)
        {
            return true;
        }
        if(n < 0)
        {
            if(errno == EINTR || errno == EAGAIN)
            {
                continue;
            }
            if(errno == ECONNRESET)
            {
                return true;
            }
            throw std::runtime_error("recv failed");
        }
        //如果接收到数据 说明连接仍然有效 继续等待EOF
    }
    return false;
}

void runHeartbeatTests(EventLoop& loop, ChatService& chat)
{
    ClientSocket alice(connectClient(PORT));
    ClientSocket bob(connectClient(PORT));
    TestMessageReceiver aliceReceiver(alice.getFd());
    TestMessageReceiver bobReceiver(bob.getFd());
    
    //Test1:两个客户端正常登录
    sendCommand(alice.getFd(),"LOGIN Alice");
    expectMessage(aliceReceiver,"LOGIN_OK");
    
    sendCommand(bob.getFd(),"LOGIN Bob");
    expectMessage(bobReceiver,"LOGIN_OK");
    waitForOnlineCount(loop, chat, 2, 3s);
    std::cout<<"[pass] Two clients login"<<std::endl;

    //Test2:心跳请求与响应
    sendCommand(alice.getFd(),"PING");
    expectMessage(aliceReceiver,"PONG");
    std::cout<<"[pass] Heartbeat PING/PONG"<<std::endl;

    //Test3:Alice持续心跳 Bob保持空闲
    //8*400ms = 3.2s 超过2s空闲超时
    for(int i=0; i<8; ++i)
    {
        std::this_thread::sleep_for(400ms);
        sendCommand(alice.getFd(),"PING");
        expectMessage(aliceReceiver,"PONG");
    }
    std::cout<<"[pass] Heartbeat keeps Alice alive"<<std::endl;

    //Test4:Bob因为空闲而断开
    if(!waitForDisconnect(bob.getFd(), 3000))
    {
        throw std::runtime_error("Bob was not disconnected after idle timeout");
    }
    std::cout<<"[pass] idle Bob disconnected"<<std::endl;

    //Test5:Bob的用户会话应当被删除
    waitForOnlineCount(loop,chat,1,3s);
    std::cout<<"[pass] Bob's session removed"<<std::endl;

    //Test6:Alice仍能正常处理业务消息
    sendCommand(alice.getFd(),"BROADCAST Hello");
    expectMessage(aliceReceiver,"BROADCAST_OK");
    std::cout<<"[pass] Active Alice still works"<<std::endl;
}

int main()
{
    try
    {
        EventLoop loop;
        ChatService chat(loop);
        TcpServer server(loop,PORT,3,std::chrono::seconds(2));
        server.setMessageCallback([&loop,&chat](const TcpServer::ConnectionPtr& connection,const std::string& message)
        {
            if(!loop.isInLoopThread())
            {
                throw std::logic_error("Message callback must be called in the loop thread");
            }
            chat.handleMessage(connection,message);
        });
        server.setConnectionCloseCallback([&chat](int fd)
        {
            chat.removeConnection(fd);
        });
        server.start();
        std::exception_ptr testError;
        std::thread worker([&]()
        {
            try
            {
                runHeartbeatTests(loop,chat);
            }
            catch(...)
            {
                testError = std::current_exception();
            }
            //无论测试成功或者失败 都请求正常关闭服务器
            loop.queueInLoop([&chat,&server]()
            {
                chat.stop();
                server.stop();
            });
        });
        //主线程运行Main Reactor
        loop.run();
        worker.join();
        if(testError)
        {
            std::rethrow_exception(testError);
        }
        std::cout<<"All heartbeat timeout tests passed!"<<std::endl;
        return 0;
    }
    catch(const std::exception& e)
    {
        std::cerr<<"[failed]"<<e.what()<<std::endl;
        return 1;
    }
}