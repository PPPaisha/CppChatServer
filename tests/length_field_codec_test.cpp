//------------------------------------------------------
// 文件名:length_field_codec_test.cpp
// 创建日期:2026-11-6
// 创建者:pppaisha
// 文件描述:长度字段编解码器测试文件
//------------------------------------------------------
#include "net/length_field_codec.h"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>

int main()
{
    //测试一 编码与完整消息解析
    LengthFieldCodec codec;
    std::string frame = LengthFieldCodec::encode("Hello");
    auto messages = codec.feed(frame.data(),frame.size());
    assert(messages.size() == 1);
    assert(messages[0] == "Hello");
    assert(codec.bufferedBytes() == 0);

    //测试二 拆包
    LengthFieldCodec splitCodec;
    auto part1 = splitCodec.feed(frame.data(),2);
    assert(part1.empty());
    auto part2 = splitCodec.feed(frame.data()+2,frame.size()-2);
    assert(part2.size() == 1);
    assert(part2[0] == "Hello");

    //测试三 粘包
    LengthFieldCodec stickyCodec;
    std::string combined = LengthFieldCodec::encode("ABC")+LengthFieldCodec::encode("DEF");
    auto result = stickyCodec.feed(combined.data(),combined.size());
    assert(result.size() == 2);
    assert(result[0] == "ABC");
    assert(result[1] == "DEF");

    //测试四 空消息
    LengthFieldCodec emptyCodec;
    std::string emptyFrame = LengthFieldCodec::encode("");
    auto emptyResult = emptyCodec.feed(emptyFrame.data(),emptyFrame.size());
    assert(emptyResult.size() == 1);
    assert(emptyResult[0].empty());

    //测试五 超长消息
    bool rejected = false;
    try
    {
        LengthFieldCodec::encode(std::string(LengthFieldCodec::MAX_FRAME_SIZE + 1, 'x'));
    }
    catch(const std::length_error&)
    {
        rejected = true;
    }
    assert(rejected);
    std::cout<<"LengthFieldCodec test passed!"<<std::endl;
    return 0;
}