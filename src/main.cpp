//------------------------------------------------------
// 文件名:main.cpp
// 创建日期:2026-11-26
// 创建者:pppaisha
// 文件描述:主程序入口
//------------------------------------------------------
#include "net/event_loop.h"
#include "net/tcp_server.h"
#include "net/signal_handler.h"

#include "chat/chat_service.h"

#include "utils/server_config.h"
#include "utils/async_logger.h"

#include <iostream>
#include <string>
#include <stdexcept>

int main(int argc, char* argv[])
{
    try
    {
        SignalHandler::blockSignals();
        if(argc > 2)
        {
            throw std::invalid_argument("Usage:chat_server [config_file]");
        }
        const std::string configPath = argc == 2 ? argv[1] : "config/server.conf";
        ServerConfig config = ServerConfig::load(configPath);
        AsyncLogger logger(config.logFile, config.logLevel);
        {
            EventLoop loop;
            ChatService chat(loop, &logger);
            TcpServer server(loop,config.port,config.ioThreads,config.idleTimeout,&logger);
            server.setMessageCallback([&chat](const TcpServer::ConnectionPtr& connection, const std::string& message)
            {
                chat.handleMessage(connection, message);
            });
            server.setConnectionCloseCallback([&chat](int fd)
            {
                chat.removeConnection(fd);
            });
            bool stopping = false;
            SignalHandler signalHandler(loop, [&chat,&server,&logger,&stopping](int signo)
            {
                if(stopping)
                {
                    return;
                }
                stopping = true;
                logger.log(LogLevel::INFO, "Shutdown signal received:" + std::to_string(signo));
                chat.stop();
                server.stop();
            });
            server.start();
            std::cout<<"CppChatServer started on port "<< config.port <<std::endl;
            std::cout<<"Press Ctrl+C to stop the server."<<std::endl;
            loop.run();
        }
        logger.log(LogLevel::INFO, "Server resources released");
        logger.stop();
        std::cout<<"CppChatServer stopped successfully."<<std::endl;
        return 0;
    }
    catch(const std::exception& e)
    {
        std::cerr<<"Server error:"<<e.what()<<std::endl;
        return 1;
    }
}