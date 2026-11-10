//------------------------------------------------------
// 文件名:length_field_codec.cpp
// 创建日期:2026-11-10
// 创建者:pppaisha
// 文件描述:长度字段编解码器实现文件
//------------------------------------------------------
#include "net/length_field_codec.h"

#include <arpa/inet.h>
#include <cstring>
#include <stdexcept>

std::string LengthFieldCodec::encode(const std::string& message)
{
    if(message.size() > MAX_FRAME_SIZE)
    {
        throw std::length_error("Message size exceeds maximum frame size");
    }
    std::uint32_t length = htonl(static_cast<std::uint32_t>(message.size()));
    std::string frame(HEADER_SIZE,'\0');
    std::memcpy(frame.data(),&length,HEADER_SIZE);
    frame.append(message);
    return frame;
}

std::vector<std::string> LengthFieldCodec::feed(const char* data, std::size_t length)
{
    if(length > 0 && data == nullptr)
    {
        throw std::invalid_argument("Data pointer is null");
    }
    m_buffer.append(data == nullptr ? "" : data,length);
    std::vector<std::string> messages;
    while(m_buffer.size() >= HEADER_SIZE)
    {
        std::uint32_t networkLength = 0;
        std::memcpy(&networkLength,m_buffer.data(),HEADER_SIZE);
        std::uint32_t bodyLength = ntohl(networkLength);
        if(bodyLength > MAX_FRAME_SIZE)
        {
            throw std::length_error("Frame size exceeds maximum frame size");
        }
        std::size_t frameLength = HEADER_SIZE + static_cast<std::size_t>(bodyLength);
        if(m_buffer.size() < frameLength)
        {
            break;
        }
        messages.emplace_back(m_buffer.data() + HEADER_SIZE, bodyLength);
        m_buffer.erase(0, frameLength);
    }
    return messages;
}

std::size_t LengthFieldCodec::bufferedBytes() const noexcept
{
    return m_buffer.size();
}