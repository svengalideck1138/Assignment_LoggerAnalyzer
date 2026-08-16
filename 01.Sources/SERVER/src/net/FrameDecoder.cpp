#include "net/FrameDecoder.h"

#include <algorithm>
#include <cstring>

namespace byda {

// ------------------------------------------------------------ PayloadWriter

void PayloadWriter::str16(const std::string& s) {
    // u16 길이 접두이므로 65535 바이트를 넘길 수 없다.
    const std::size_t n = std::min<std::size_t>(s.size(), 0xFFFFu);
    u16(static_cast<std::uint16_t>(n));
    buf_.insert(buf_.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(n));
}

// ------------------------------------------------------------ PayloadReader

bool PayloadReader::need(std::size_t n) {
    if (!ok_ || remaining() < n) {
        ok_ = false;
        return false;
    }
    return true;
}

bool PayloadReader::u8(std::uint8_t& out) {
    if (!need(1)) return false;
    out = buf_[pos_];
    pos_ += 1;
    return true;
}

bool PayloadReader::u16(std::uint16_t& out) {
    if (!need(2)) return false;
    out = load_be16(buf_.data() + pos_);
    pos_ += 2;
    return true;
}

bool PayloadReader::u32(std::uint32_t& out) {
    if (!need(4)) return false;
    out = load_be32(buf_.data() + pos_);
    pos_ += 4;
    return true;
}

bool PayloadReader::u64(std::uint64_t& out) {
    if (!need(8)) return false;
    out = load_be64(buf_.data() + pos_);
    pos_ += 8;
    return true;
}

bool PayloadReader::str16(std::string& out) {
    std::uint16_t n = 0;
    if (!u16(n)) return false;
    if (!need(n)) return false;
    out.assign(reinterpret_cast<const char*>(buf_.data() + pos_), n);
    pos_ += n;
    return true;
}

// ------------------------------------------------------------- FrameDecoder

void FrameDecoder::reset_frame() noexcept {
    state_ = State::Header;
    head_got_ = 0;
    payload_got_ = 0;
    payload_.clear();  // capacity 는 유지되므로 다음 프레임에서 재할당이 없다
    streaming_ = false;
    error_ = ErrorCode::None;
}

FrameDecoder::Status FrameDecoder::feed(const char* data, std::size_t len,
                                        std::size_t& consumed) {
    consumed = 0;

    if (state_ == State::Complete) {
        // 호출자가 reset_frame() 을 잊었다. 아무것도 소비하지 않는다.
        return Status::Ready;
    }

    if (state_ == State::Header) {
        const std::size_t want = kHeaderSize - head_got_;
        const std::size_t take = std::min(want, len);
        std::memcpy(head_raw_.data() + head_got_, data, take);
        head_got_ += take;
        consumed += take;

        if (head_got_ < kHeaderSize) {
            return Status::NeedMore;
        }

        decode_header(head_raw_, header_);

        if (header_.magic != kMagic) {
            error_ = ErrorCode::BadMagic;
            return Status::Failed;
        }
        if (header_.version != kVersion) {
            error_ = ErrorCode::BadVersion;
            return Status::Failed;
        }
        // 이 검사가 없으면 조작된 payload_len 하나로 거대한 resize 를 시도하게 된다.
        if (header_.payload_len > kMaxPayload) {
            error_ = ErrorCode::PayloadTooLarge;
            return Status::Failed;
        }

        if (header_.payload_len == 0) {
            state_ = State::Complete;
            return Status::Ready;
        }

        // 큰 payload 는 버퍼에 모으지 않고 sink 로 흘려보낸다.
        streaming_ = (sink_ != nullptr) && sink_->stream_payload(header_.type, header_.payload_len);
        if (!streaming_) {
            payload_.resize(static_cast<std::size_t>(header_.payload_len));
        }
        payload_got_ = 0;
        state_ = State::Payload;
    }

    if (state_ == State::Payload) {
        const std::size_t total = static_cast<std::size_t>(header_.payload_len);
        const std::size_t want = total - payload_got_;
        const std::size_t avail = len - consumed;
        const std::size_t take = std::min(want, avail);

        if (take > 0) {
            if (streaming_) {
                // 수신 버퍼를 그대로 넘긴다. 복사도 할당도 없다.
                sink_->payload_chunk(data + consumed, take);
            } else {
                std::memcpy(payload_.data() + payload_got_, data + consumed, take);
            }
            payload_got_ += take;
            consumed += take;
        }

        if (payload_got_ < total) {
            return Status::NeedMore;
        }

        state_ = State::Complete;
        return Status::Ready;
    }

    return Status::NeedMore;
}

// ---------------------------------------------------------------- serialize

void serialize_frame(MsgType type, const std::vector<std::uint8_t>& payload,
                     std::vector<std::uint8_t>& out) {
    FrameHeader h;
    h.type = type;
    h.payload_len = static_cast<std::uint64_t>(payload.size());

    HeaderBytes raw{};
    encode_header(h, raw);

    out.clear();
    out.reserve(kHeaderSize + payload.size());
    out.insert(out.end(), raw.begin(), raw.end());
    out.insert(out.end(), payload.begin(), payload.end());
}

void serialize_error(ErrorCode code, const std::string& message,
                     std::vector<std::uint8_t>& out) {
    PayloadWriter w;
    w.u16(static_cast<std::uint16_t>(code));
    w.str16(message);
    serialize_frame(MsgType::Error, w.bytes(), out);
}

}  // namespace byda
