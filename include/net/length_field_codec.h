//------------------------------------------------------
// 文件名:length_field_codec.h
// 创建日期:2026-11-10
// 创建者:pppaisha
// 文件描述:长度字段编解码器头文件
//------------------------------------------------------
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class LengthFieldCodec
{
public:
    static constexpr std::size_t HEADER_SIZE = 4;
    static constexpr std::size_t MAX_FRAME_SIZE = 1024*1024;
    
    //编码一条消息 : 4字节长度 + 消息体
    static std::string encode(const std::string& message);
    //追加收到的字节 并解析所有完整消息
    std::vector<std::string> feed(const char* data, std::size_t length);

    std::size_t bufferedBytes() const noexcept;

private:
    std::string m_buffer;//用于保存已经收到 但是尚未组成完整消息的字节

};