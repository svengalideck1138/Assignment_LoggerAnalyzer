// 바이트 조각 -> 라인.
//
// 네트워크에서 오는 조각은 라인 경계와 아무 관계가 없다. 한 조각의
// 끝이 라인 중간일 수도 있고, 한 라인이 세 조각에 걸칠 수도 있다.
// LineSplitter 가 그 경계를 흡수한다.
//
// 설계 포인트 두 가지:
//
//  1) 정상 경로에 복사가 없다.
//     조각 안에서 완결되는 라인은 memchr 로 개행을 찾아 그 구간을
//     string_view 로 그대로 넘긴다. 수신 버퍼를 가리키기만 한다.
//     조각 경계에 걸친 라인만 carry_ 에 이어 붙인다. 실측상 500MB /
//     평균 144바이트 = 348만 라인 중 조각 경계에 걸리는 것은
//     256KiB 마다 한 줄, 약 1900줄뿐이다.
//
//  2) carry_ 는 재할당되지 않는다.
//     생성 시 reserve(max_line + 1) 해두고 이후 clear() 만 하므로
//     capacity 가 유지된다.
//
//  그리고 개행이 하나도 없는 스트림에 대한 방어가 필수다. 가드가 없으면
//  carry_ 가 500MB 로 자라 메모리 제약을 정면으로 위반한다. max_line 을
//  넘어가면 그 라인은 버리고 다음 개행까지 건너뛴다.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace byda {

class LineSplitter {
public:
    // 실측 최대 라인 길이는 163 바이트다. 8192 는 그 50배로,
    // 정상 데이터를 자를 위험 없이 폭주만 막는 값이다.
    static constexpr std::size_t kDefaultMaxLine = 8192;

    explicit LineSplitter(std::size_t max_line = kDefaultMaxLine) : max_line_(max_line) {
        carry_.reserve(max_line_ + 1);
    }

    void reset() {
        carry_.clear();
        discarding_ = false;
        lines_ = 0;
        oversize_ = 0;
    }

    std::uint64_t lines() const noexcept { return lines_; }
    std::uint64_t oversize() const noexcept { return oversize_; }
    std::size_t carry_size() const noexcept { return carry_.size(); }

    // 조각 하나를 넣는다. 완성된 라인마다
    //   on_line(std::string_view line, std::size_t raw_bytes)
    // 를 부른다. raw_bytes 는 개행과 (있었다면) CR 까지 포함한 원본
    // 바이트 수로, 호출자가 스트림 내 오프셋을 추적하는 데 쓴다.
    // line 은 이 호출이 끝나면 무효가 되므로 보관하려면 복사해야 한다.
    template <class Fn>
    void feed(const char* data, std::size_t len, Fn&& on_line) {
        std::size_t pos = 0;

        // 초장문 라인을 버리는 중이면 다음 개행까지 건너뛴다.
        if (discarding_) {
            const void* nl = std::memchr(data, '\n', len);
            if (nl == nullptr) {
                return;
            }
            pos = static_cast<std::size_t>(static_cast<const char*>(nl) - data) + 1;
            discarding_ = false;
        }

        // 앞 조각에서 넘어온 부분 라인이 있으면 먼저 완성시킨다.
        if (!carry_.empty()) {
            const void* nl = std::memchr(data + pos, '\n', len - pos);
            if (nl == nullptr) {
                if (carry_.size() + (len - pos) > max_line_) {
                    ++oversize_;
                    carry_.clear();
                    discarding_ = true;
                } else {
                    carry_.append(data + pos, len - pos);
                }
                return;
            }

            const std::size_t upto =
                static_cast<std::size_t>(static_cast<const char*>(nl) - (data + pos));

            if (carry_.size() + upto > max_line_) {
                ++oversize_;
                carry_.clear();
            } else {
                const std::size_t raw = carry_.size() + upto + 1;
                carry_.append(data + pos, upto);
                emit(carry_, raw, on_line);
                carry_.clear();
            }
            pos += upto + 1;
        }

        // 여기서부터가 정상 경로다. 복사 없이 수신 버퍼를 직접 가리킨다.
        while (pos < len) {
            const void* nl = std::memchr(data + pos, '\n', len - pos);
            if (nl == nullptr) {
                break;
            }
            const std::size_t upto =
                static_cast<std::size_t>(static_cast<const char*>(nl) - (data + pos));
            emit(std::string_view(data + pos, upto), upto + 1, on_line);
            pos += upto + 1;
        }

        // 남은 꼬리는 다음 조각과 이어 붙인다.
        if (pos < len) {
            const std::size_t tail = len - pos;
            if (tail > max_line_) {
                ++oversize_;
                discarding_ = true;
            } else {
                carry_.assign(data + pos, tail);
            }
        }
    }

    // 스트림이 끝났을 때 호출한다. 파일이 개행으로 끝나지 않는 경우
    // 마지막 라인이 carry_ 에 남아 있다.
    template <class Fn>
    void finish(Fn&& on_line) {
        if (!carry_.empty()) {
            emit(carry_, carry_.size(), on_line);
            carry_.clear();
        }
        discarding_ = false;
    }

private:
    template <class Fn>
    void emit(std::string_view line, std::size_t raw_bytes, Fn&& on_line) {
        // CRLF 입력도 받아들인다. 실측상 이 파일에는 CR 이 0개지만
        // 다른 환경에서 만든 로그를 그대로 넣어도 동작해야 한다.
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        ++lines_;
        on_line(line, raw_bytes);
    }

    std::size_t max_line_;
    std::string carry_;
    bool discarding_ = false;
    std::uint64_t lines_ = 0;
    std::uint64_t oversize_ = 0;
};

}  // namespace byda

