#include "parse/LogLine.h"

#include <array>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace byda {
namespace {

inline bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

inline bool is_ident(char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || is_digit(c) || c == '_';
}

inline unsigned digit2(const char* p) noexcept {
    return static_cast<unsigned>(p[0] - '0') * 10u + static_cast<unsigned>(p[1] - '0');
}

inline unsigned digit4(const char* p) noexcept {
    return static_cast<unsigned>(p[0] - '0') * 1000u + static_cast<unsigned>(p[1] - '0') * 100u +
           static_cast<unsigned>(p[2] - '0') * 10u + static_cast<unsigned>(p[3] - '0');
}

// 타임스탬프 블록은 폭이 고정이다:
//   [2026-06-19_22:00:00.045000]
//    0123456789...           27
constexpr std::size_t kTsEnd = 27;

bool check_timestamp(const char* s) noexcept {
    if (s[0] != '[') return false;
    if (!is_digit(s[1]) || !is_digit(s[2]) || !is_digit(s[3]) || !is_digit(s[4])) return false;
    if (s[5] != '-') return false;
    if (!is_digit(s[6]) || !is_digit(s[7])) return false;
    if (s[8] != '-') return false;
    if (!is_digit(s[9]) || !is_digit(s[10])) return false;
    if (s[11] != '_') return false;
    if (!is_digit(s[12]) || !is_digit(s[13])) return false;
    if (s[14] != ':') return false;
    if (!is_digit(s[15]) || !is_digit(s[16])) return false;
    if (s[17] != ':') return false;
    if (!is_digit(s[18]) || !is_digit(s[19])) return false;
    if (s[20] != '.') return false;
    for (int i = 21; i <= 26; ++i) {
        if (!is_digit(s[i])) return false;
    }
    return s[kTsEnd] == ']';
}

// 5개 모듈명은 (길이, 첫 글자) 쌍이 전부 유일하다.
//   (19,'R') RadarTrackNodeState    (18,'A') AntennaProfileSpec
//   (18,'S') SectorSchedulerRTS     (19,'D') DetectionTaskRunner
//   (21,'B') BeamSteerCtrlUnitImpl
// 해시맵도 문자열 비교 루프도 필요 없다. switch 두 번과 memcmp 한 번이면 끝난다.
bool lookup_module(std::string_view name, Module& out) noexcept {
    switch (name.size()) {
        case 18:
            if (name[0] == 'A' && std::memcmp(name.data(), "AntennaProfileSpec", 18) == 0) {
                out = Module::AntennaProfileSpec;
                return true;
            }
            if (name[0] == 'S' && std::memcmp(name.data(), "SectorSchedulerRTS", 18) == 0) {
                out = Module::SectorSchedulerRTS;
                return true;
            }
            return false;
        case 19:
            if (name[0] == 'R' && std::memcmp(name.data(), "RadarTrackNodeState", 19) == 0) {
                out = Module::RadarTrackNodeState;
                return true;
            }
            if (name[0] == 'D' && std::memcmp(name.data(), "DetectionTaskRunner", 19) == 0) {
                out = Module::DetectionTaskRunner;
                return true;
            }
            return false;
        case 21:
            if (name[0] == 'B' && std::memcmp(name.data(), "BeamSteerCtrlUnitImpl", 21) == 0) {
                out = Module::BeamSteerCtrlUnitImpl;
                return true;
            }
            return false;
        default:
            return false;
    }
}

// "spd[<value>]" 를 찾아 값을 뽑는다.
//
// 주의할 점 두 가지:
//  1) 같은 라인에 unitAddr[4181] 과 advDelta[62750.000000] 도 있다.
//     반드시 "spd[" 를 앵커로 잡아야 한다.
//  2) "advSpd[" 같은 다른 키의 꼬리를 잡으면 안 된다.
//     'spd[' 바로 앞 문자가 식별자 문자면 거부한다.
bool find_spd(std::string_view payload, double& value, bool& out_of_range) noexcept {
    std::size_t pos = 0;
    while (true) {
        const std::size_t at = payload.find("spd[", pos);
        if (at == std::string_view::npos) {
            return false;
        }
        if (at > 0 && is_ident(payload[at - 1])) {
            pos = at + 4;  // 다른 키의 꼬리다. 계속 찾는다.
            continue;
        }

        const std::size_t begin = at + 4;
        const std::size_t close = payload.find(']', begin);
        if (close == std::string_view::npos) {
            return false;
        }

        const std::string_view token = payload.substr(begin, close - begin);
        if (token.empty() || token.size() >= 64) {
            return false;
        }

        // strtod 에 넘기기 전에 형태를 직접 검사한다.
        // "NONE", "X", "nan", "inf", "0x10" 같은 값을 여기서 걸러낸다.
        std::size_t i = 0;
        if (token[i] == '+' || token[i] == '-') ++i;
        std::size_t digits = 0;
        while (i < token.size() && is_digit(token[i])) { ++i; ++digits; }
        if (i < token.size() && token[i] == '.') {
            ++i;
            while (i < token.size() && is_digit(token[i])) { ++i; ++digits; }
        }
        if (digits == 0 || i != token.size()) {
            return false;
        }

        // string_view 는 널 종료가 아니므로 스택 버퍼로 복사한다.
        // std::stod 를 쓰지 않는 이유: 임시 std::string 을 만들어 힙을
        // 건드리고, 실패 시 예외를 던진다. 둘 다 핫 패스에서 피해야 한다.
        std::array<char, 64> buf{};
        std::memcpy(buf.data(), token.data(), token.size());
        buf[token.size()] = '\0';

        errno = 0;
        char* end = nullptr;
        const double v = std::strtod(buf.data(), &end);

        if (end != buf.data() + token.size() || !std::isfinite(v)) {
            return false;
        }
        if (errno == ERANGE || v < kSpdMin || v > kSpdMax) {
            // 화이트리스트를 통과했더라도 값이 물리적으로 말이 안 되면
            // 평균에서 뺀다. 화이트리스트에 이은 이중 방어다.
            out_of_range = true;
            return false;
        }

        value = v;
        return true;
    }
}

}  // namespace

const char* module_name(Module m) noexcept {
    switch (m) {
        case Module::RadarTrackNodeState: return "RadarTrackNodeState";
        case Module::AntennaProfileSpec: return "AntennaProfileSpec";
        case Module::SectorSchedulerRTS: return "SectorSchedulerRTS";
        case Module::DetectionTaskRunner: return "DetectionTaskRunner";
        case Module::BeamSteerCtrlUnitImpl: return "BeamSteerCtrlUnitImpl";
    }
    return "?";
}

const char* reason_key(Reason r) noexcept {
    switch (r) {
        case Reason::Ok: return "accepted";
        case Reason::TooShort: return "rejected_too_short";
        case Reason::MissingOpenBracket: return "rejected_missing_open_bracket";
        case Reason::BadTimestamp: return "rejected_bad_timestamp";
        case Reason::GarbagePayload: return "rejected_garbage_payload";
        case Reason::MissingCloseBracket: return "rejected_missing_close_bracket";
        case Reason::MissingBydaTag: return "rejected_missing_byda_tag";
        case Reason::BadModuleName: return "rejected_bad_module_name";
        case Reason::UnknownModule: return "rejected_unknown_module";
        case Reason::Count: break;
    }
    return "rejected_unknown";
}

const char* reason_text(Reason r) noexcept {
    switch (r) {
        case Reason::Ok: return "Header valid and module in whitelist";
        case Reason::TooShort: return "Line shorter than the minimum valid header";
        case Reason::MissingOpenBracket: return "Line does not start with '['";
        case Reason::BadTimestamp: return "Timestamp block does not match the fixed layout";
        case Reason::GarbagePayload: return "Timestamp present but no PID/TID/SEQ structure";
        case Reason::MissingCloseBracket: return "PID/TID/SEQ field not terminated by ']'";
        case Reason::MissingBydaTag: return "Missing the ' BYDA::' tag";
        case Reason::BadModuleName: return "Module name malformed or not followed by ': '";
        case Reason::UnknownModule: return "Header valid but module is not in the whitelist";
        case Reason::Count: break;
    }
    return "Unknown";
}

Reason parse_log_line(std::string_view line, ParsedLine& out) noexcept {
    out = ParsedLine{};

    // S0: 길이 가드
    if (line.size() < kMinLineLength) {
        return line.empty() ? Reason::TooShort
                            : (line[0] != '[' ? Reason::MissingOpenBracket : Reason::TooShort);
    }

    const char* s = line.data();

    // S1: 타임스탬프 고정 오프셋 검사
    //     HeadBraceLoss(선두 '[' 누락)가 여기서 걸린다.
    if (s[0] != '[') {
        return Reason::MissingOpenBracket;
    }
    if (!check_timestamp(s)) {
        return Reason::BadTimestamp;
    }

    // S2: "[digits]" 세 번 (PID, TID, SEQ)
    //     OpenBraceLeak(SEQ 뒤 ']' 누락)와 쓰레기 라인이 여기서 갈린다.
    std::size_t i = kTsEnd + 1;
    for (int field = 0; field < 3; ++field) {
        if (i >= line.size() || s[i] != '[') {
            // 첫 필드부터 없으면 헤더 구조 자체가 없는 쓰레기 라인이다.
            return (field == 0) ? Reason::GarbagePayload : Reason::MissingCloseBracket;
        }
        ++i;
        std::size_t digits = 0;
        while (i < line.size() && is_digit(s[i])) {
            ++i;
            ++digits;
            if (digits > 20) {
                return Reason::MissingCloseBracket;
            }
        }
        if (digits == 0 || i >= line.size() || s[i] != ']') {
            return Reason::MissingCloseBracket;
        }
        ++i;
    }

    // S3: " BYDA::"
    static constexpr char kTag[] = " BYDA::";
    constexpr std::size_t kTagLen = sizeof(kTag) - 1;
    if (i + kTagLen > line.size() || std::memcmp(s + i, kTag, kTagLen) != 0) {
        return Reason::MissingBydaTag;
    }
    i += kTagLen;

    // S4: 모듈명 + ": "
    const std::size_t mod_begin = i;
    while (i < line.size() && is_ident(s[i])) {
        ++i;
    }
    const std::size_t mod_len = i - mod_begin;
    if (mod_len == 0 || i + 1 >= line.size() || s[i] != ':' || s[i + 1] != ' ') {
        return Reason::BadModuleName;
    }
    const std::string_view mod_token(s + mod_begin, mod_len);
    out.module_token = mod_token;
    i += 2;

    // S5: 화이트리스트. 이 과제의 정답 게이트.
    //     CorruptPayload 와 BeyondLimit 이 여기서 격추된다.
    Module module{};
    if (!lookup_module(mod_token, module)) {
        return Reason::UnknownModule;
    }
    out.module = module;

    // 시간 버킷: YYYYMMDDHH
    const unsigned year = digit4(s + 1);
    const unsigned month = digit2(s + 6);
    const unsigned day = digit2(s + 9);
    const unsigned hour = digit2(s + 12);
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23) {
        return Reason::BadTimestamp;
    }
    out.bucket = static_cast<std::uint64_t>(year) * 1000000ull +
                 static_cast<std::uint64_t>(month) * 10000ull +
                 static_cast<std::uint64_t>(day) * 100ull + static_cast<std::uint64_t>(hour);

    // S6: 페이로드에서 속도값 추출 (있는 라인만)
    const std::string_view payload(s + i, line.size() - i);
    double spd = 0.0;
    bool oor = false;
    if (find_spd(payload, spd, oor)) {
        out.has_spd = true;
        out.spd = spd;
    }
    out.spd_out_of_range = oor;

    return Reason::Ok;
}

}  // namespace byda
