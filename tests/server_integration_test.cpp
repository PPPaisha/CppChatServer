//------------------------------------------------------
// 文件名:server_integration_test.cpp
// 创建日期:2026-11-26
// 创建者:pppaisha
// 文件描述:服务器集成测试文件
//------------------------------------------------------
#include "utils/server_config.h"
#include "net/length_field_codec.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <signal.h>
#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <utility>

using namespace std::chrono_literals;

constexpr uint16_t PORT = 12089;

const std::string CONFIG_PATH =
    "server_integration_test.conf";

const std::string LOG_PATH =
    "server_integration_test.log";

const std::string SERVER_PATH =
    "./build/chat_server";

void require(bool condition,
             const std::string& message)
{
    if(!condition)
    {
        throw std::runtime_error(message);
    }
}

void writeConfig(const std::string& content)
{
    std::ofstream file(
        CONFIG_PATH,
        std::ios::trunc
    );

    require(
        file.is_open(),
        "Failed to create config file"
    );

    file << content;
    file.close();

    require(
        static_cast<bool>(file),
        "Failed to write config file"
    );
}

void expectInvalidConfig(
    const std::string& content
)
{
    writeConfig(content);

    bool rejected = false;

    try
    {
        ServerConfig::load(CONFIG_PATH);
    }
    catch(const std::exception&)
    {
        rejected = true;
    }

    require(
        rejected,
        "Invalid config was accepted: " + content
    );
}

void runConfigTests()
{
    writeConfig(
        "# Server configuration\n"
        "\n"
        " port = 12089 \n"
        "io_threads = 2\n"
        "idle_timeout = 60\n"
        "log_level = DEBUG\n"
        "log_file = server_integration_test.log\n"
    );

    ServerConfig config =
        ServerConfig::load(CONFIG_PATH);

    require(config.port == PORT,
            "Incorrect port");

    require(config.ioThreads == 2,
            "Incorrect I/O thread count");

    require(config.idleTimeout.count() == 60,
            "Incorrect idle timeout");

    require(config.logLevel == LogLevel::DEBUG,
            "Incorrect log level");

    require(config.logFile == LOG_PATH,
            "Incorrect log file");

    std::cout
        << "[PASS] Valid configuration"
        << std::endl;

    writeConfig("# Use default configuration\n");

    config = ServerConfig::load(CONFIG_PATH);

    require(
        config.port == 12088 &&
        config.ioThreads == 3 &&
        config.idleTimeout.count() == 30 &&
        config.logLevel == LogLevel::INFO &&
        config.logFile == "server.log",
        "Incorrect default configuration"
    );

    writeConfig("io_threads=0\n");

    config = ServerConfig::load(CONFIG_PATH);

    require(
        config.ioThreads == 0,
        "Single Reactor configuration rejected"
    );

    std::cout
        << "[PASS] Default and zero-thread configuration"
        << std::endl;

    expectInvalidConfig("port=0\n");
    expectInvalidConfig("port=65536\n");
    expectInvalidConfig("port=-1\n");
    expectInvalidConfig("port=123abc\n");

    expectInvalidConfig("io_threads=-1\n");
    expectInvalidConfig("io_threads=65\n");

    expectInvalidConfig("idle_timeout=0\n");
    expectInvalidConfig("idle_timeout=86401\n");

    expectInvalidConfig("log_level=UNKNOWN\n");
    expectInvalidConfig("log_file=\n");

    expectInvalidConfig("port=12089\nport=12090\n");
    expectInvalidConfig("unknown_key=123\n");
    expectInvalidConfig("invalid_line\n");

    std::cout
        << "[PASS] Invalid configuration rejection"
        << std::endl;

    require(
        ::unlink(CONFIG_PATH.c_str()) == 0,
        "Failed to remove test configuration"
    );

    bool missingRejected = false;

    try
    {
        ServerConfig::load(CONFIG_PATH);
    }
    catch(const std::exception&)
    {
        missingRejected = true;
    }

    require(
        missingRejected,
        "Missing config was accepted"
    );

    std::cout
        << "[PASS] Missing configuration rejection"
        << std::endl;
}

class ServerProcess
{
public:
    ServerProcess()
    {
        m_pid = ::fork();

        if(m_pid < 0)
        {
            throw std::runtime_error("fork failed");
        }

        if(m_pid == 0)
        {
            ::execl(
                SERVER_PATH.c_str(),
                SERVER_PATH.c_str(),
                CONFIG_PATH.c_str(),
                static_cast<char*>(nullptr)
            );

            ::_exit(127);
        }
    }

    ~ServerProcess()
    {
        if(m_pid > 0)
        {
            ::kill(m_pid,SIGKILL);

            int status = 0;

            while(::waitpid(m_pid,&status,0) < 0)
            {
                if(errno != EINTR)
                {
                    break;
                }
            }
        }
    }

    ServerProcess(const ServerProcess&) = delete;

    ServerProcess& operator=(
        const ServerProcess&
    ) = delete;

    void sendSignal(int signo)
    {
        require(
            m_pid > 0,
            "Server process is not running"
        );

        require(
            ::kill(m_pid,signo) == 0,
            "Failed to send signal"
        );
    }

    void ensureRunning()
    {
        int status = 0;

        pid_t result = ::waitpid(
            m_pid,
            &status,
            WNOHANG
        );

        if(result == m_pid)
        {
            m_pid = -1;

            throw std::runtime_error(
                "Server exited unexpectedly"
            );
        }

        if(result < 0 && errno != EINTR)
        {
            throw std::runtime_error(
                "waitpid failed"
            );
        }
    }

    void waitForExit()
    {
        const auto deadline =
            std::chrono::steady_clock::now() + 5s;

        while(std::chrono::steady_clock::now()
              < deadline)
        {
            int status = 0;

            pid_t result = ::waitpid(
                m_pid,
                &status,
                WNOHANG
            );

            if(result == m_pid)
            {
                m_pid = -1;

                require(
                    WIFEXITED(status),
                    "Server did not exit normally"
                );

                require(
                    WEXITSTATUS(status) == 0,
                    "Server returned non-zero exit code"
                );

                return;
            }

            if(result < 0 && errno != EINTR)
            {
                throw std::runtime_error(
                    "waitpid failed"
                );
            }

            std::this_thread::sleep_for(50ms);
        }

        throw std::runtime_error(
            "Server shutdown timeout"
        );
    }

private:
    pid_t m_pid = -1;
};

class ClientSocket
{
public:
    explicit ClientSocket(int fd)
        :m_fd(fd)
    {}

    ~ClientSocket()
    {
        if(m_fd >= 0)
        {
            ::close(m_fd);
        }
    }

    ClientSocket(const ClientSocket&) = delete;

    ClientSocket& operator=(
        const ClientSocket&
    ) = delete;

    int getFd() const noexcept
    {
        return m_fd;
    }

private:
    int m_fd;
};

int connectClient(ServerProcess& server)
{
    const auto deadline =
        std::chrono::steady_clock::now() + 5s;

    while(std::chrono::steady_clock::now()
          < deadline)
    {
        server.ensureRunning();

        int fd = ::socket(
            AF_INET,
            SOCK_STREAM | SOCK_CLOEXEC,
            0
        );

        require(fd >= 0,"socket failed");

        sockaddr_in address{};

        address.sin_family = AF_INET;
        address.sin_port = htons(PORT);
        address.sin_addr.s_addr =
            htonl(INADDR_LOOPBACK);

        if(::connect(
            fd,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)
        ) == 0)
        {
            return fd;
        }

        int error = errno;

        ::close(fd);

        if(error != ECONNREFUSED &&
           error != EINTR)
        {
            throw std::runtime_error(
                "connect failed"
            );
        }

        std::this_thread::sleep_for(50ms);
    }

    throw std::runtime_error(
        "Server startup timeout"
    );
}

void sendCommand(
    int fd,
    const std::string& message
)
{
    std::string data =
        LengthFieldCodec::encode(message);

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

class TestMessageReceiver
{
public:
    explicit TestMessageReceiver(int fd)
        :m_fd(fd)
    {}

    std::string receiveMessage()
    {
        const auto deadline =
            std::chrono::steady_clock::now() + 3s;

        while(m_messages.empty())
        {
            const auto now =
                std::chrono::steady_clock::now();

            require(
                now < deadline,
                "Receive timeout"
            );

            int remaining = static_cast<int>(
                std::chrono::duration_cast<
                    std::chrono::milliseconds
                >(deadline-now).count()
            );

            pollfd pfd{};

            pfd.fd = m_fd;
            pfd.events = POLLIN;

            int result = ::poll(
                &pfd,1,remaining
            );

            if(result == 0)
            {
                throw std::runtime_error(
                    "poll timeout"
                );
            }

            if(result < 0)
            {
                if(errno == EINTR)
                {
                    continue;
                }

                throw std::runtime_error(
                    "poll failed"
                );
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
                   errno == EAGAIN)
                {
                    continue;
                }

                throw std::runtime_error(
                    "recv failed"
                );
            }

            auto messages = m_decoder.feed(
                buffer,
                static_cast<std::size_t>(n)
            );

            for(auto& message : messages)
            {
                m_messages.push_back(
                    std::move(message)
                );
            }
        }

        std::string message =
            std::move(m_messages.front());

        m_messages.erase(m_messages.begin());

        return message;
    }

private:
    int m_fd;

    LengthFieldCodec m_decoder;

    std::vector<std::string> m_messages;
};

void expectMessage(
    TestMessageReceiver& receiver,
    const std::string& expected
)
{
    std::string actual =
        receiver.receiveMessage();

    require(
        actual == expected,
        "Expected: " + expected +
        ", received: " + actual
    );
}

bool waitForDisconnect(int fd)
{
    const auto deadline =
        std::chrono::steady_clock::now() + 3s;

    while(std::chrono::steady_clock::now()
          < deadline)
    {
        pollfd pfd{};

        pfd.fd = fd;
        pfd.events = POLLIN;

        int result = ::poll(&pfd,1,100);

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

            throw std::runtime_error(
                "poll failed"
            );
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
               errno == EAGAIN)
            {
                continue;
            }

            throw std::runtime_error(
                "recv failed"
            );
        }
    }

    return false;
}

void verifyLogs(int signo)
{
    std::ifstream file(LOG_PATH);

    require(
        file.is_open(),
        "Failed to open server log"
    );

    std::string logs(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );

    require(
        logs.find("User login succeeded")
            != std::string::npos,
        "Missing login log"
    );

    require(
        logs.find(
            "Shutdown signal received:" +
            std::to_string(signo)
        ) != std::string::npos,
        "Missing shutdown signal log"
    );

    require(
        logs.find("TcpServer stopped")
            != std::string::npos,
        "Missing server stopped log"
    );

    require(
        logs.find("Server resources released")
            != std::string::npos,
        "Missing resource release log"
    );

    require(
        logs.find("Version28PrivateMessage")
            == std::string::npos,
        "Private message leaked into logs"
    );
}

void runServerTests(int signo)
{
    writeConfig(
        "port=12089\n"
        "io_threads=2\n"
        "idle_timeout=60\n"
        "log_level=DEBUG\n"
        "log_file=server_integration_test.log\n"
    );

    {
        std::ofstream file(
            LOG_PATH,
            std::ios::trunc
        );

        require(
            file.is_open(),
            "Failed to clear log file"
        );
    }

    ServerProcess server;

    ClientSocket alice(connectClient(server));
    ClientSocket bob(connectClient(server));

    TestMessageReceiver aliceReceiver(
        alice.getFd()
    );

    TestMessageReceiver bobReceiver(
        bob.getFd()
    );

    std::cout
        << "[PASS] Server process started"
        << std::endl;

    sendCommand(alice.getFd(),"LOGIN Alice");
    expectMessage(aliceReceiver,"LOGIN_OK");

    sendCommand(bob.getFd(),"LOGIN Bob");
    expectMessage(bobReceiver,"LOGIN_OK");

    std::cout
        << "[PASS] Client login"
        << std::endl;

    sendCommand(
        alice.getFd(),
        "SEND Bob Version28PrivateMessage"
    );

    expectMessage(
        bobReceiver,
        "FROM Alice Version28PrivateMessage"
    );

    expectMessage(aliceReceiver,"SEND_OK");

    std::cout
        << "[PASS] Private message"
        << std::endl;

    sendCommand(alice.getFd(),"PING");
    expectMessage(aliceReceiver,"PONG");

    std::cout
        << "[PASS] Heartbeat"
        << std::endl;

    server.sendSignal(signo);

    require(
        waitForDisconnect(alice.getFd()),
        "Alice was not disconnected"
    );

    require(
        waitForDisconnect(bob.getFd()),
        "Bob was not disconnected"
    );

    std::cout
        << "[PASS] Active connections closed"
        << std::endl;

    server.waitForExit();

    std::cout
        << "[PASS] Normal process exit"
        << std::endl;

    verifyLogs(signo);

    std::cout
        << "[PASS] Shutdown log verification"
        << std::endl;
}

int main()
{
    try
    {
        runConfigTests();

        std::cout
            << "\n===== SIGINT Test ====="
            << std::endl;

        runServerTests(SIGINT);

        std::cout
            << "\n===== SIGTERM Test ====="
            << std::endl;

        runServerTests(SIGTERM);

        std::cout
            << "\nAll server integration tests passed!"
            << std::endl;

        return 0;
    }
    catch(const std::exception& e)
    {
        std::cerr
            << "[FAILED] "
            << e.what()
            << std::endl;

        return 1;
    }
}
