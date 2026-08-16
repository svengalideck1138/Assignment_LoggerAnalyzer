// Zhenyu_LoggerAnalyzer - wire protocol definition
//
// 이 헤더는 Linux 서버가 사용하는 와이어 포맷의 단일 진실 소스다.
// C# 클라이언트는 01.Sources/CLIENT/CSHARP/Network/Protocol.cs 에서 동일한 규약을 구현하고,
// C++ ImGui 클라이언트는 이 헤더를 그대로 include 한다 (01.Sources/CLIENT/CPP).
// 플랫폼 헤더(<winsock2.h> / <arpa/inet.h>)에 의존하지 않도록
// 바이트 순서 변환을 직접 구현한다.
//
//   프레임 = 16바이트 고정 헤더 + payload
//
//   offset  size  field
//     0      4    magic        0x42594441 ('B''Y''D''A')
//     4      1    version      0x01
//     5      1    msg_type
//     6      2    flags        (예약, 0)
//     8      8    payload_len  big endian
//    16      -    payload[payload_len]
//
// 정수는 전부 빅엔디안(네트워크 바이트 순서)으로 싣는다.
// 구조체를 그대로 memcpy 하지 않는 이유: 패딩/정렬/ABI 차이에 의존하지
// 않기 위해서다. 바이트 단위로 직접 읽고 쓰면 컴파일러와 플랫폼이 달라도
// 동일한 바이트열이 나온다.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace byda {

inline constexpr std::uint32_t kMagic = 0x42594441u;
inline constexpr std::uint8_t kVersion = 0x01u;
inline constexpr std::size_t kHeaderSize = 16u;

// 한 프레임의 payload 상한. 이 검사가 없으면 조작된 헤더 하나로
// 거대한 버퍼를 잡으려 시도하게 된다.
inline constexpr std::uint64_t kMaxPayload = 4u * 1024u * 1024u;

// 업로드 청크 기본 크기 (HELLO_ACK 으로 클라이언트에 통보).
inline constexpr std::uint32_t kDefaultMaxChunk = 1u * 1024u * 1024u;

enum class MsgType : std::uint8_t {
    Hello = 0x01,
    HelloAck = 0x02,

    UploadBegin = 0x10,
    UploadBeginAck = 0x11,
    UploadChunk = 0x12,
    UploadEnd = 0x13,
    UploadAck = 0x14,

    AnalyzeProgress = 0x20,
    AnalyzeDone = 0x21,

    ResultBegin = 0x31,
    ResultChunk = 0x32,
    ResultEnd = 0x33,

    Cancel = 0x40,
    Error = 0x7E,
    Bye = 0x7F,
};

enum class ErrorCode : std::uint16_t {
    None = 0,
    BadMagic = 1,
    BadVersion = 2,
    UnexpectedType = 3,
    PayloadTooLarge = 4,
    SizeMismatch = 5,
    InternalIo = 6,
    IdleTimeout = 8,
    Cancelled = 9,
    ServerBusy = 10,
    ShuttingDown = 11,
};

inline const char* error_text(ErrorCode c) noexcept {
    switch (c) {
        case ErrorCode::None: return "none";
        case ErrorCode::BadMagic: return "bad magic";
        case ErrorCode::BadVersion: return "unsupported protocol version";
        case ErrorCode::UnexpectedType: return "unexpected message type for current state";
        case ErrorCode::PayloadTooLarge: return "payload too large";
        case ErrorCode::SizeMismatch: return "declared size mismatch";
        case ErrorCode::InternalIo: return "internal io error";
        case ErrorCode::IdleTimeout: return "idle timeout";
        case ErrorCode::Cancelled: return "cancelled by client";
        case ErrorCode::ServerBusy: return "server busy";
        case ErrorCode::ShuttingDown: return "server shutting down";
    }
    return "unknown";
}

inline const char* msg_name(MsgType t) noexcept {
    switch (t) {
        case MsgType::Hello: return "HELLO";
        case MsgType::HelloAck: return "HELLO_ACK";
        case MsgType::UploadBegin: return "UPLOAD_BEGIN";
        case MsgType::UploadBeginAck: return "UPLOAD_BEGIN_ACK";
        case MsgType::UploadChunk: return "UPLOAD_CHUNK";
        case MsgType::UploadEnd: return "UPLOAD_END";
        case MsgType::UploadAck: return "UPLOAD_ACK";
        case MsgType::AnalyzeProgress: return "ANALYZE_PROGRESS";
        case MsgType::AnalyzeDone: return "ANALYZE_DONE";
        case MsgType::ResultBegin: return "RESULT_BEGIN";
        case MsgType::ResultChunk: return "RESULT_CHUNK";
        case MsgType::ResultEnd: return "RESULT_END";
        case MsgType::Cancel: return "CANCEL";
        case MsgType::Error: return "ERROR";
        case MsgType::Bye: return "BYE";
    }
    return "UNKNOWN";
}

// ---------------------------------------------------------------- byte order

inline void store_be16(std::uint8_t* p, std::uint16_t v) noexcept {
    // uint16_t 는 시프트 전에 int 로 정수 승격되므로, 부호 변환 경고를
    // 피하려면 명시적으로 unsigned 로 올려 두고 계산한다.
    const unsigned u = v;
    p[0] = static_cast<std::uint8_t>((u >> 8) & 0xFFu);
    p[1] = static_cast<std::uint8_t>(u & 0xFFu);
}

inline void store_be32(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    p[3] = static_cast<std::uint8_t>(v & 0xFFu);
}

inline void store_be64(std::uint8_t* p, std::uint64_t v) noexcept {
    for (int i = 0; i < 8; ++i) {
        const unsigned shift = static_cast<unsigned>(56 - 8 * i);
        p[i] = static_cast<std::uint8_t>((v >> shift) & 0xFFu);
    }
}

inline std::uint16_t load_be16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) |
                                      static_cast<std::uint16_t>(p[1]));
}

inline std::uint32_t load_be32(const std::uint8_t* p) noexcept {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
           static_cast<std::uint32_t>(p[3]);
}

inline std::uint64_t load_be64(const std::uint8_t* p) noexcept {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | static_cast<std::uint64_t>(p[i]);
    }
    return v;
}

// ------------------------------------------------------------------- header

struct FrameHeader {
    std::uint32_t magic = kMagic;
    std::uint8_t version = kVersion;
    MsgType type = MsgType::Bye;
    std::uint16_t flags = 0u;
    std::uint64_t payload_len = 0u;
};

using HeaderBytes = std::array<std::uint8_t, kHeaderSize>;

inline void encode_header(const FrameHeader& h, HeaderBytes& out) noexcept {
    store_be32(out.data() + 0, h.magic);
    out[4] = h.version;
    out[5] = static_cast<std::uint8_t>(h.type);
    store_be16(out.data() + 6, h.flags);
    store_be64(out.data() + 8, h.payload_len);
}

inline void decode_header(const HeaderBytes& in, FrameHeader& h) noexcept {
    h.magic = load_be32(in.data() + 0);
    h.version = in[4];
    h.type = static_cast<MsgType>(in[5]);
    h.flags = load_be16(in.data() + 6);
    h.payload_len = load_be64(in.data() + 8);
}

}  // namespace byda

