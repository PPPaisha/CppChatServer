//------------------------------------------------------
// 文件名:chat_service.cpp
// 创建日期:2026-11-18
// 创建者:pppaisha
// 文件描述:客户端聊天服务实现文件
//------------------------------------------------------
#include "chat/chat_service.h"
#include "utils/async_logger.h"

#include "net/event_loop.h"
#include "net/tcp_connection.h"

#include <stdexcept>
#include <utility>
#include <vector>

ChatService::ChatService(EventLoop& loop,AsyncLogger* logger)
    :m_loop(loop),m_logger(logger)
{
}

void ChatService::ensureLoopThread() const
{
    if(!m_loop.isInLoopThread())
    {
        throw std::logic_error("ChatService must run in EventLoop thread");
    }
}

bool ChatService::registerUser(const std::string& userId, const std::shared_ptr<TcpConnection>& connection)
{
    ensureLoopThread();
    if(userId.empty() || !connection ||connection->getFd() < 0)
    {
        return false;
    }
    int fd = connection->getFd();
    if(m_sessions.find(userId) != m_sessions.end() || m_fdToUser.find(fd) != m_fdToUser.end())
    {
        return false;
    }
    UserSession session{userId,fd,connection};
    auto [it, inserted] = m_sessions.emplace(userId, std::move(session));
    if(!inserted)
    {
        return false;
    }
    try
    {
        auto result = m_fdToUser.emplace(fd,userId);
        if(!result.second)
        {
            m_sessions.erase(it);
            return false;
        }
    }
    catch(...)
    {
        m_sessions.erase(it);
        throw;
    }
    return true;
}

bool ChatService::isRegisteredConnection(int fd) const
{
    ensureLoopThread();
    return m_fdToUser.find(fd) != m_fdToUser.end();
}

bool ChatService::removeUser(const std::string& userId)
{
    ensureLoopThread();
    auto it = m_sessions.find(userId);
    if(it == m_sessions.end())
    {
        return false;
    }
    const int fd = it->second.fd;
    auto fdIt = m_fdToUser.find(fd);
    if(fdIt != m_fdToUser.end() && fdIt->second == userId)
    {
        m_fdToUser.erase(fdIt);
    }
    m_sessions.erase(it);
    if(m_logger)
    {
        m_logger->log(LogLevel::INFO,"User session removed, fd=" + std::to_string(fd));
    }
    return true;
}

void ChatService::removeConnection(int fd)
{
    ensureLoopThread();
    auto it = m_fdToUser.find(fd);
    if(it == m_fdToUser.end())
    {
        return;
    }
    const std::string userId = it->second;
    removeUser(userId);
}

bool ChatService::hasUser(const std::string& userId) const
{
    ensureLoopThread();
    return m_sessions.find(userId) != m_sessions.end();
}

std::size_t ChatService::onlineCount() const 
{
    ensureLoopThread();
    return m_sessions.size();
}

bool ChatService::sendToUser(const std::string& userId, const std::string& message)
{
    ensureLoopThread();
    auto it = m_sessions.find(userId);
    if(it == m_sessions.end())
    {return false;}
    auto connection = it->second.connection.lock();
    if(!connection)
    {
        removeUser(userId);
        return false;
    }
    return connection->trySendMessage(message);
}

void ChatService::sendToUserAsync(const std::string& userId, const std::string& message, SendResultCallback callback)
{
    ensureLoopThread();
    auto it = m_sessions.find(userId);
    if(it == m_sessions.end())
    {
        if(callback)
        {
            callback(false);
        }
        return;
    }
    auto connection = it->second.connection.lock();
    if(!connection)
    {
        removeUser(userId);
        if(callback)
        {
            callback(false);
        }
        return;
    }
    connection->sendMessageAsync(message, std::move(callback));
}

std::size_t ChatService::broadcast(const std::string& senderId, const std::string& message)
{
    ensureLoopThread();
    std::vector<std::shared_ptr<TcpConnection>> targets;
    for(const auto& [userId,session] : m_sessions)
    {
        if(userId == senderId)
        {
            continue;
        }
        if(auto connection = session.connection.lock())
        {
            targets.push_back(std::move(connection));
        }
    }
    for(const auto& connection: targets)
    {
        connection->sendMessageAsync(message);
    }
    return targets.size();
}

void ChatService::handleMessage(const std::shared_ptr<TcpConnection>& connection, const std::string& message)
{
    ensureLoopThread();
    if(m_stopping)
    {
        return;
    }
    if(!connection)
    {
        return;
    }
    const int fd = connection->getFd();
    //LOGIN用户名
    if(message.rfind("LOGIN ",0) == 0)
    {
        std::string userId = message.substr(6);
        bool valid = !userId.empty() && userId.find(" ") == std::string::npos;
        bool success = valid && registerUser(userId,connection);
        if(m_logger)
        {
            if(success)
            {
                m_logger->log(LogLevel::INFO,"User login succeeded, fd=" + std::to_string(fd));
            }
            else
            {
                m_logger->log(LogLevel::WARN,"User login failed, fd=" + std::to_string(fd));
            }
        }
        connection->sendMessageAsync(success ? "LOGIN_OK" : "LOGIN_FAIL");
        return;
    }
    //其他命令必须先登录
    auto senderIt = m_fdToUser.find(fd);
    if(senderIt == m_fdToUser.end())
    {
        if(m_logger)
        {
            m_logger->log(LogLevel::WARN,"Command from unregistered connection, fd=" + std::to_string(fd));
        }
        connection->sendMessageAsync("ERROR Not logged in");
        return;
    }
    //复制用户名 避免发送过程中会话删除导致引用失效
    const std::string senderId = senderIt->second;
    //心跳检测: 已登录客户端发送PING 服务器回复PONG
    if(message == "PING")
    {
        if(m_logger)
        {
            m_logger->log(LogLevel::DEBUG,"Heartbeat received, fd=" + std::to_string(fd));
        }
        connection->sendMessageAsync("PONG");
        return;
    }
    //SEND 接收者 消息内容
    if(message.rfind("SEND ",0) == 0)
    {
        std::size_t separator = message.find(' ',5);
        if(separator == std::string::npos || separator == 5 || separator+1 >= message.size())
        {
            if(m_logger)
            {
                m_logger->log(LogLevel::WARN,"Invalid SEND format, fd=" + std::to_string(fd));
            }
            connection->sendMessageAsync("ERROR: Invalid SEND format");
            return;
        }
        std::string targetId = message.substr(5,separator-5);
        std::string content = message.substr(separator+1);
        if(m_logger)
        {
            m_logger->log(LogLevel::DEBUG,"Private message requested , senderFd=" + std::to_string(fd) + ", bytes=" + std::to_string(content.size()));
        }
        
        std::weak_ptr<TcpConnection> weak = connection;
        EventLoop* baseLoop = &m_loop;
        sendToUserAsync(targetId,"FROM " + senderId + " " + content,[this,baseLoop,weak,fd](bool accepted)
        {
            baseLoop->queueInLoop([this,weak,accepted,fd]()
            {
                if(m_stopping)
                {
                    return;
                }
                if(m_logger)
                {
                    m_logger->log(accepted ? LogLevel::DEBUG : LogLevel::WARN, std::string(
                        accepted ? "Private message accepted" : "Private message rejected") + ", senderFd=" + std::to_string(fd));
                }
                if(auto sender = weak.lock())
                {
                    sender->sendMessageAsync(accepted ? "SEND_OK" : "SEND_FAILED");
                }
            });
        });
        return;
    }
    //BROADCAST 消息内容
    if(message.rfind("BROADCAST ",0) == 0)
    {
        std::string content = message.substr(10);
        if(content.empty())
        {
            connection->sendMessageAsync("ERROR: Invalid BROADCAST format");
            return;
        }
        std::size_t count = broadcast(senderId,"BROADCAST_FROM " + senderId + " " + content);
        if(m_logger)
        {
            m_logger->log(LogLevel::DEBUG,"Broadcast requested, senderFd=" + std::to_string(fd) + ", targets=" + std::to_string(count) + ", bytes=" + std::to_string(content.size()));
        }
        connection->sendMessageAsync("BROADCAST_OK");
        return;
    }
    if(m_logger)
    {
        m_logger->log(LogLevel::WARN, "Unknown command, fd=" + std::to_string(fd));
    }
    connection->sendMessageAsync("ERROR Invalid command");
}

void ChatService::stop()
{
    ensureLoopThread();
    if(m_stopping)
    {
        return;
    }
    m_stopping = true;
    if(m_logger)
    {
        m_logger->log(LogLevel::INFO, "ChatService stopping, onlineUsers=" + std::to_string(m_sessions.size()));
    }
}