// Zhenyu_LoggerAnalyzer C++ client - payload 직렬화 헬퍼.
//
// C# 클라이언트의 PayloadWriter / PayloadReader 와 동일한 규약.
// 와이어 포맷 자체(엔디안 변환, 헤더)는 서버의 net/Protocol.h 를
// 그대로 include 해서 쓴다. 여기는 payload 조립/해석만 담당한다.
//
// PayloadReader 는 남은 바이트가 모자라면 그 시점부터 모든 읽기가
// 실패로 고정된다. 잘리거나 조작된 프레임이 와도 범위 밖 접근으로
// 터지지 않는다.

#pragma once

#include <net/Protocol.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bydacli {

class PayloadWriter {
public:
    void u8(std::uint8_t v) { buf_.push_back(v); }

    void u16(std::uint16_t v) {
        std::uint8_t t[2];
        byda::store_be16(t, v);
        buf_.insert(buf_.end(), t, t + 2);
    }

    void u32(std::uint32_t v) {
        std::uint8_t t[4];
        byda::store_be32(t, v);
        buf_.insert(buf_.end(), t, t + 4);
    }

    void u64(std::uint64_t v) {
        std::uint8_t t[8];
        byda::store_be64(t, v);
        buf_.insert(buf_.end(), t, t + 8);
    }

    // 길이(u16) 접두 UTF-8 문자열.
    void str16(std::string_view s) {
        std::size_t n = s.size();
        if (n > 0xFFFFu) {
            n = 0xFFFFu;
        }
        u16(static_cast<std::uint16_t>(n));
        buf_.insert(buf_.end(), s.data(), s.data() + n);
    }

    const std::vector<std::uint8_t>& bytes() const noexcept { return buf_; }

private:
    std::vector<std::uint8_t> buf_;
};

class PayloadReader {
public:
    explicit PayloadReader(const std::vector<std::uint8_t>& buf) noexcept
        : buf_(buf.data()), size_(buf.size()) {}

    bool ok() const noexcept { return ok_; }
    std::size_t remaining() const noexcept { return size_ - pos_; }

    bool u8(std::uint8_t& v) noexcept {
        if (!need(1)) return false;
        v = buf_[pos_];
        pos_ += 1;
        return true;
    }

    bool u16(std::uint16_t& v) noexcept {
        if (!need(2)) return false;
        v = byda::load_be16(buf_ + pos_);
        pos_ += 2;
        return true;
    }

    bool u32(std::uint32_t& v) noexcept {
        if (!need(4)) return false;
        v = byda::load_be32(buf_ + pos_);
        pos_ += 4;
        return true;
    }

    bool u64(std::uint64_t& v) noexcept {
        if (!need(8)) return false;
        v = byda::load_be64(buf_ + pos_);
        pos_ += 8;
        return true;
    }

    bool str16(std::string& s) {
        std::uint16_t n = 0;
        if (!u16(n)) return false;
        if (!need(n)) return false;
        s.assign(reinterpret_cast<const char*>(buf_ + pos_), n);
        pos_ += n;
        return true;
    }

private:
    bool need(std::size_t n) noexcept {
        if (!ok_ || remaining() < n) {
            ok_ = false;
            return false;
        }
        return true;
    }

    const std::uint8_t* buf_;
    std::size_t size_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

// 바이트 수를 사람이 읽기 좋은 문자열로 ("482.8 MiB").
std::string human_bytes(std::uint64_t n);

}  // namespace bydacli
