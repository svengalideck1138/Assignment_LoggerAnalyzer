// 프레임 조립/해석.
//
// libuv 의 read 콜백은 "임의 크기의 조각"을 준다. 한 번에 프레임 절반이
// 올 수도 있고, 프레임 세 개가 한꺼번에 올 수도 있다. FrameDecoder 가
// 그 경계를 흡수한다. 이 구조 덕분에 recv_exact 같은 블로킹 루프가 필요 없다.
//
// 동적 메모리는 std::vector 만 쓴다. 수동 할당은 없다.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "net/IPayloadSink.h"
#include "net/Protocol.h"

namespace byda {

// payload 를 순서대로 쌓는다.
class PayloadWriter {
public:
    void u8(std::uint8_t v) { buf_.push_back(v); }

    void u16(std::uint16_t v) {
        const std::size_t at = grow(2);
        store_be16(buf_.data() + at, v);
    }

    void u32(std::uint32_t v) {
        const std::size_t at = grow(4);
        store_be32(buf_.data() + at, v);
    }

    void u64(std::uint64_t v) {
        const std::size_t at = grow(8);
        store_be64(buf_.data() + at, v);
    }

    // 길이(u16) 접두 문자열.
    void str16(const std::string& s);

    const std::vector<std::uint8_t>& bytes() const noexcept { return buf_; }

private:
    std::size_t grow(std::size_t n) {
        const std::size_t at = buf_.size();
        buf_.resize(at + n);
        return at;
    }

    std::vector<std::uint8_t> buf_;
};

// payload 를 순서대로 읽는다.
// 남은 바이트가 모자라면 그 시점부터 모든 읽기가 실패로 고정된다.
// 잘리거나 조작된 프레임이 와도 버퍼 밖을 읽지 않는다.
class PayloadReader {
public:
    explicit PayloadReader(const std::vector<std::uint8_t>& buf) noexcept : buf_(buf) {}

    bool u8(std::uint8_t& out);
    bool u16(std::uint16_t& out);
    bool u32(std::uint32_t& out);
    bool u64(std::uint64_t& out);
    bool str16(std::string& out);

    bool ok() const noexcept { return ok_; }
    std::size_t remaining() const noexcept { return buf_.size() - pos_; }

private:
    bool need(std::size_t n);

    const std::vector<std::uint8_t>& buf_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

// 바이트 스트림 -> 프레임.
//
// 사용법:
//   std::size_t off = 0;
//   while (off < len) {
//       std::size_t used = 0;
//       const auto st = dec.feed(data + off, len - off, used);
//       off += used;
//       if (st == FrameDecoder::Status::Failed)   { /* dec.error() */ break; }
//       if (st == FrameDecoder::Status::NeedMore) { break; }
//       handle(dec.header(), dec.payload());
//       dec.reset_frame();
//   }
class FrameDecoder {
public:
    enum class Status {
        NeedMore,  // 조각이 더 필요하다
        Ready,     // 프레임 하나가 완성됐다
        Failed,    // 프로토콜 위반 - 연결을 끊어야 한다
    };

    Status feed(const char* data, std::size_t len, std::size_t& consumed);

    // 큰 payload 를 흘려보낼 통로. 설정하지 않으면 전부 버퍼링한다.
    void set_sink(IPayloadSink* sink) noexcept { sink_ = sink; }

    const FrameHeader& header() const noexcept { return header_; }

    // 스트리밍된 프레임이면 비어 있다. streamed() 로 구분한다.
    const std::vector<std::uint8_t>& payload() const noexcept { return payload_; }

    bool streamed() const noexcept { return streaming_; }
    ErrorCode error() const noexcept { return error_; }

    // 완성된 프레임을 처리한 뒤 다음 프레임을 받기 위해 호출한다.
    void reset_frame() noexcept;

private:
    enum class State {
        Header,
        Payload,
        Complete,
    };

    State state_ = State::Header;
    HeaderBytes head_raw_{};
    std::size_t head_got_ = 0;

    FrameHeader header_{};
    std::vector<std::uint8_t> payload_;
    std::size_t payload_got_ = 0;

    IPayloadSink* sink_ = nullptr;
    bool streaming_ = false;

    ErrorCode error_ = ErrorCode::None;
};

// 프레임 하나를 연속된 바이트열(헤더 + payload)로 직렬화한다.
// libuv 의 uv_write 는 버퍼가 write 완료 콜백까지 살아 있어야 하므로,
// 호출자가 그 수명을 소유할 수 있도록 vector 를 채워 준다.
void serialize_frame(MsgType type, const std::vector<std::uint8_t>& payload,
                     std::vector<std::uint8_t>& out);

void serialize_error(ErrorCode code, const std::string& message,
                     std::vector<std::uint8_t>& out);

}  // namespace byda
