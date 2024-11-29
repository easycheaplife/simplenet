#pragma once
#include <cstdint>
#include <vector>

#pragma pack(push, 1)
struct MessageHeader {
    uint32_t magic{0x12345678};  // 魔数
    uint32_t length{0};          // 消息体长度
    uint16_t type{0};           // 消息类型
    uint16_t version{1};        // 协议版本
};
#pragma pack(pop)

class Message {
public:
    static const size_t HEADER_SIZE = sizeof(MessageHeader);
    MessageHeader header;
    std::vector<char> body;
};
