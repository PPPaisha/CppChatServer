//------------------------------------------------------
// 文件名:logger_integration_test.cpp
// 创建日期:2026-11-25
// 创建者:pppaisha
// 文件描述:异步日志系统与多Reactor服务器集成测试
//------------------------------------------------------

#include "net/event_loop.h"
#include "net/tcp_server.h"
#include "net/length_field_codec.h"
#include "chat/chat_service.h"
#include "utils/async_logger.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <deque>

using namespace std::chrono_literals;

constexpr uint16_t PORT = 12087;
constexpr int TIMEOUT_MS = 3000;

const std::string LOG_FILE = "logger_integration_test.log";

using Clock = std::chrono::steady_clock;

//======================================================
// 第一部分：客户端网络通信辅助函数
//======================================================

void sendAll(int fd, const std::string& data)
{
    std::size_t offset = 0;

    while(offset < data.size())
    {
        ssize_t n = ::send(
            fd,
            data.data() + offset,
            data.size() - offset,
            MSG_NOSIGNAL
        );

        if(n > 0)
        {
            offset += static_cast<std::size_t>(n);
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

void sendCommand(int fd, const std::string& message)
{
    sendAll(fd, LengthFieldCodec::encode(message));
}

int connectClient(uint16_t port)
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);

    if(fd < 0)
    {
        throw std::runtime_error("socket failed");
    }

    sockaddr_in address{};

    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if(::connect(
        fd,
        reinterpret_cast<sockaddr*>(&address),
        sizeof(address)
    ) < 0)
    {
        ::close(fd);

        throw std::runtime_error("connect failed");
    }

    return fd;
}

//======================================================
// 第二部分：客户端Socket资源管理
//======================================================

class ClientSocket
{
public:
    explicit ClientSocket(int fd)
        :m_fd(fd)
    {
    }

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

//======================================================
// 第三部分：完整消息接收器
//======================================================

class TestMessageReceiver
{
public:
    explicit TestMessageReceiver(int fd)
        :m_fd(fd)
    {
    }

    std::string receiveMessage(int timeoutMs = TIMEOUT_MS)
    {
        const auto deadline =
            Clock::now() + std::chrono::milliseconds(timeoutMs);

        while(m_messages.empty())
        {
            const auto now = Clock::now();

            if(now >= deadline)
            {
                throw std::runtime_error(
                    "Timeout waiting for message"
                );
            }

            int remaining = static_cast<int>(
                std::chrono::ceil<std::chrono::milliseconds>(
                    deadline - now
                ).count()
            );

            pollfd pfd{};

            pfd.fd = m_fd;
            pfd.events = POLLIN;

            int result = ::poll(&pfd, 1, remaining);

            if(result == 0)
            {
                throw std::runtime_error("poll timeout");
            }

            if(result < 0)
            {
                if(errno == EINTR)
                {
                    continue;
                }

                throw std::runtime_error("poll failed");
            }

            char buffer[4096];

            ssize_t n = ::recv(
                m_fd,
                buffer,
                sizeof(buffer),
                MSG_DONTWAIT
            );

            if(n == 0)
            {
                throw std::runtime_error(
                    "Connection closed unexpectedly"
                );
            }

            if(n < 0)
            {
                if(errno == EINTR ||
                   errno == EAGAIN ||
                   errno == EWOULDBLOCK)
                {
                    continue;
                }

                throw std::runtime_error("recv failed");
            }

            auto messages = m_decoder.feed(
                buffer,
                static_cast<std::size_t>(n)
            );

            for(auto& message : messages)
            {
                m_messages.push_back(std::move(message));
            }
        }

        std::string message =
            std::move(m_messages.front());

        m_messages.pop_front();

        return message;
    }

private:
    int m_fd;

    LengthFieldCodec m_decoder;

    std::deque<std::string> m_messages;
};

void expectMessage(
    TestMessageReceiver& receiver,
    const std::string& expected
)
{
    std::string actual = receiver.receiveMessage();

    if(actual != expected)
    {
        throw std::runtime_error(
            "Expected: " + expected +
            ", actual: " + actual
        );
    }
}

//======================================================
// 第四部分：跨线程查询服务器状态
//======================================================

std::size_t queryOnlineCount(
    EventLoop& loop,
    ChatService& chat
)
{
    auto promise =
        std::make_shared<std::promise<std::size_t>>();

    auto future = promise->get_future();

    loop.queueInLoop([&chat,promise]()
    {
        try
        {
            promise->set_value(chat.onlineCount());
        }
        catch(...)
        {
            promise->set_exception(
                std::current_exception()
            );
        }
    });

    if(future.wait_for(3s) != std::future_status::ready)
    {
        throw std::runtime_error(
            "Timeout querying online count"
        );
    }

    return future.get();
}

bool waitForDisconnect(int fd, int timeoutMs)
{
    const auto deadline =
        Clock::now() + std::chrono::milliseconds(timeoutMs);

    while(Clock::now() < deadline)
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

        ssize_t n = ::recv(
            fd,
            buffer,
            sizeof(buffer),
            MSG_DONTWAIT
        );

        if(n == 0)
        {
            return true;
        }

        if(n < 0)
        {
            if(errno == ECONNRESET)
            {
                return true;
            }

            if(errno == EINTR ||
               errno == EAGAIN ||
               errno == EWOULDBLOCK)
            {
                continue;
            }

            throw std::runtime_error("recv failed");
        }
    }

    return false;
}

//======================================================
// 第五部分：日志文件检查工具
//======================================================

void clearLogFile()
{
    std::ofstream output(
        LOG_FILE,
        std::ios::out | std::ios::trunc
    );

    if(!output)
    {
        throw std::runtime_error(
            "Failed to clear log file"
        );
    }
}

std::string readLogFile()
{
    std::ifstream input(LOG_FILE);

    if(!input.is_open())
    {
        throw std::runtime_error(
            "Failed to open log file"
        );
    }

    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    );
}

void expectLog(
    const std::string& logs,
    const std::string& expected
)
{
    if(logs.find(expected) == std::string::npos)
    {
        throw std::runtime_error(
            "Missing log: " + expected
        );
    }

    std::cout << "[pass] Log found: "
              << expected << std::endl;
}

//======================================================
// 第六部分：网络与业务功能统一测试
//======================================================

void runIntegrationTests(
    EventLoop& loop,
    ChatService& chat,
    TcpServer& server
)
{
    ClientSocket alice(connectClient(PORT));
    ClientSocket bob(connectClient(PORT));

    TestMessageReceiver aliceReceiver(alice.getFd());
    TestMessageReceiver bobReceiver(bob.getFd());

    // 测试1：未登录发送命令
    sendCommand(alice.getFd(),"PING");

    expectMessage(
        aliceReceiver,
        "ERROR Not logged in"
    );

    std::cout << "[pass] Unregistered command rejected"
              << std::endl;

    // 测试2：两个客户端登录
    sendCommand(alice.getFd(),"LOGIN Alice");
    expectMessage(aliceReceiver,"LOGIN_OK");

    sendCommand(bob.getFd(),"LOGIN Bob");
    expectMessage(bobReceiver,"LOGIN_OK");

    if(queryOnlineCount(loop,chat) != 2)
    {
        throw std::runtime_error(
            "Expected two online users"
        );
    }

    std::cout << "[pass] Two clients login"
              << std::endl;

    // 测试3：重复登录
    sendCommand(alice.getFd(),"LOGIN Alice");
    expectMessage(aliceReceiver,"LOGIN_FAIL");

    std::cout << "[pass] Duplicate login rejected"
              << std::endl;

    // 测试4：私聊
    sendCommand(
        alice.getFd(),
        "SEND Bob PrivateTestMessage27"
    );

    expectMessage(
        bobReceiver,
        "FROM Alice PrivateTestMessage27"
    );

    expectMessage(aliceReceiver,"SEND_OK");

    std::cout << "[pass] Private message"
              << std::endl;

    // 测试5：目标用户不存在
    sendCommand(
        alice.getFd(),
        "SEND Nobody MissingUserMessage"
    );

    expectMessage(aliceReceiver,"SEND_FAILED");

    std::cout << "[pass] Missing target rejected"
              << std::endl;

    // 测试6：广播
    sendCommand(
        alice.getFd(),
        "BROADCAST BroadcastTestMessage27"
    );

    expectMessage(
        bobReceiver,
        "BROADCAST_FROM Alice BroadcastTestMessage27"
    );

    expectMessage(aliceReceiver,"BROADCAST_OK");

    std::cout << "[pass] Broadcast message"
              << std::endl;

    // 测试7：非法命令
    sendCommand(alice.getFd(),"UNKNOWN_COMMAND");

    expectMessage(
        aliceReceiver,
        "ERROR Invalid command"
    );

    std::cout << "[pass] Unknown command rejected"
              << std::endl;

    // 刷新Bob最后一次活动时间
    sendCommand(bob.getFd(),"PING");
    expectMessage(bobReceiver,"PONG");

    // 测试8：Alice持续发送心跳
    // Bob不再发送任何消息
    for(int i = 0; i < 15; ++i)
    {
        sendCommand(alice.getFd(),"PING");

        expectMessage(aliceReceiver,"PONG");

        std::this_thread::sleep_for(400ms);
    }

    std::cout << "[pass] Heartbeat keeps Alice alive"
              << std::endl;

    // 测试9：Bob应该已经被服务器断开
    if(!waitForDisconnect(bob.getFd(),1000))
    {
        throw std::runtime_error(
            "Bob was not disconnected"
        );
    }

    std::cout << "[pass] Idle Bob disconnected"
              << std::endl;

    // 测试10：确认Bob的会话已经删除
    const auto deadline = Clock::now() + 2s;

    while(queryOnlineCount(loop,chat) != 1)
    {
        if(Clock::now() >= deadline)
        {
            throw std::runtime_error(
                "Bob session was not removed"
            );
        }

        std::this_thread::sleep_for(50ms);
    }

    std::cout << "[pass] Bob session removed"
              << std::endl;

    // 确认Alice依然可以正常通信
    sendCommand(alice.getFd(),"PING");
    expectMessage(aliceReceiver,"PONG");

    // 测试11：在Alice仍然在线时停止服务器
    loop.queueInLoop([&chat,&server]()
    {
        chat.stop();
        server.stop();
    });

    if(!waitForDisconnect(alice.getFd(),3000))
    {
        throw std::runtime_error(
            "Alice was not disconnected during shutdown"
        );
    }

    std::cout << "[pass] Active client closed during shutdown"
              << std::endl;
}

//======================================================
// 第七部分：验证日志文件内容
//======================================================

void verifyLogs()
{
    const std::string logs = readLogFile();

    expectLog(logs,"TcpServer started");
    expectLog(logs,"New connection accepted");
    expectLog(logs,"TcpConnection started");
    expectLog(logs,"Connection registered");

    expectLog(logs,"Command from unregistered connection");
    expectLog(logs,"User login succeeded");
    expectLog(logs,"User login failed");

    expectLog(logs,"Message received");
    expectLog(logs,"Private message requested");
    expectLog(logs,"Private message accepted");
    expectLog(logs,"Private message rejected");

    expectLog(logs,"Broadcast requested");
    expectLog(logs,"targets=1");
    expectLog(logs,"Unknown command");

    expectLog(logs,"Heartbeat received");
    expectLog(logs,"Idle connection timeout");

    expectLog(logs,"TcpConnection closed");
    expectLog(logs,"Connection removed");
    expectLog(logs,"User session removed");

    expectLog(logs,"ChatService stopping");
    expectLog(logs,"TcpServer stopping, activeConnections=1");
    expectLog(logs,"TcpServer stopped");

    // 检查日志等级
    expectLog(logs,"[INFO]");
    expectLog(logs,"[DEBUG]");
    expectLog(logs,"[WARN]");

    // 检查线程ID字段
    expectLog(logs,"[Thread ");

    // 确认没有记录完整聊天内容
    if(logs.find("PrivateTestMessage27") != std::string::npos ||
       logs.find("BroadcastTestMessage27") != std::string::npos)
    {
        throw std::runtime_error(
            "Private message content leaked into logs"
        );
    }

    std::cout << "[pass] Message content not logged"
              << std::endl;
}

//======================================================
// 第八部分：测试程序入口
//======================================================

int main()
{
    try
    {
        // AsyncLogger使用追加模式
        // 因此每次测试之前先清空旧日志
        clearLogFile();

        // 构造函数自动启动日志线程
        AsyncLogger logger(LOG_FILE,LogLevel::DEBUG);

        {
            EventLoop loop;

            ChatService chat(loop,&logger);

            TcpServer server(
                loop,
                PORT,
                3,
                std::chrono::seconds(4),
                &logger
            );

            server.setMessageCallback(
                [&loop,&chat](
                    const TcpServer::ConnectionPtr& connection,
                    const std::string& message
                )
                {
                    if(!loop.isInLoopThread())
                    {
                        throw std::logic_error(
                            "Message callback must run in main loop"
                        );
                    }

                    chat.handleMessage(connection,message);
                }
            );

            server.setConnectionCloseCallback(
                [&chat](int fd)
                {
                    chat.removeConnection(fd);
                }
            );

            server.start();

            std::exception_ptr testError;

            std::thread worker([&]()
            {
                try
                {
                    runIntegrationTests(loop,chat,server);
                }
                catch(...)
                {
                    testError = std::current_exception();

                    loop.queueInLoop([&chat,&server]()
                    {
                        chat.stop();
                        server.stop();
                    });
                }
            });

            loop.run();

            worker.join();

            if(testError)
            {
                std::rethrow_exception(testError);
            }

            if(server.connectionCount() != 0 ||
               chat.onlineCount() != 0)
            {
                throw std::runtime_error(
                    "Server resources were not fully cleaned"
                );
            }

            std::cout << "[pass] Server resources cleaned"
                      << std::endl;
        }

        // 网络对象全部销毁之后
        // 再停止日志线程并刷新剩余日志
        logger.stop();

        std::cout << "\n===== Verify log file =====\n"
                  << std::endl;

        verifyLogs();

        std::cout
            << "\nAll logger integration tests passed!"
            << std::endl;

        return 0;
    }
    catch(const std::exception& e)
    {
        std::cerr << "[failed] "
                  << e.what()
                  << std::endl;

        return 1;
    }
}
