// 로그 라인 한 줄의 검증과 필드 추출.
//
// 대상 포맷:
//   [YYYY-MM-DD_HH:MM:SS.ffffff][PID][TID][SEQ] BYDA::<Module>: <payload>
//
// ── 왜 정규식을 쓰지 않는가 ──────────────────────────────────────────
// 과제는 "std::string 연산 또는 정규식"을 허용한다. 348만 라인에
// std::regex 를 돌리면 분 단위가 걸린다. 반면 이 로그의 타임스탬프는
// 폭이 고정되어 있어서 인덱스를 직접 검사하면 분기 몇 개로 끝난다.
// 할당도 예외도 없다.
//
// ── 왜 계층 검증인가 ────────────────────────────────────────────────
// 훼손 라인이 어디서 걸렸는지에 따라 사유를 다르게 기록해야
// "무엇이 몇 건 잘못됐는지"를 보고할 수 있다. 단계마다 실패 사유가
// 다르므로 그대로 통계가 된다.
//
// ── 핵심: 모듈 화이트리스트 ─────────────────────────────────────────
// 헤더가 완벽하게 정상이면서 페이로드만 오염된 라인이 존재한다.
//   [...] BYDA::BeyondLimit: spd[888888888888888888888.88]
// 이 라인은 어떤 헤더 검사도 통과한다. 화이트리스트가 없으면
// 이 6줄이 정상 580,661건의 평균을 삼켜서 평균 속도가
// 137,500 이 아니라 9.18e15 로 나온다. 화이트리스트가 정답 게이트다.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace byda {

enum class Module : std::uint8_t {
    RadarTrackNodeState = 0,
    AntennaProfileSpec,
    SectorSchedulerRTS,
    DetectionTaskRunner,
    BeamSteerCtrlUnitImpl,
};

inline constexpr std::size_t kModuleCount = 5;

const char* module_name(Module m) noexcept;

// 라인이 거부된 사유. CSV 의 처분(disposition) 통계가 그대로 이 값들이다.
enum class Reason : std::uint8_t {
    Ok = 0,
    TooShort,
    MissingOpenBracket,   // 라인이 '[' 로 시작하지 않음
    BadTimestamp,         // 타임스탬프 블록이 규격과 다름
    GarbagePayload,       // 타임스탬프만 있고 PID/TID/SEQ 구조가 없음
    MissingCloseBracket,  // PID/TID/SEQ 중 하나가 ']' 로 닫히지 않음
    MissingBydaTag,       // " BYDA::" 가 없음
    BadModuleName,        // 모듈명 문자가 잘못됐거나 ": " 가 없음
    UnknownModule,        // 헤더는 정상이나 화이트리스트에 없는 모듈
    Count
};

inline constexpr std::size_t kReasonCount = static_cast<std::size_t>(Reason::Count);

// CSV 에 그대로 쓰는 짧은 키.
const char* reason_key(Reason r) noexcept;
// 사람이 읽는 설명.
const char* reason_text(Reason r) noexcept;

// 속도값 허용 범위. 화이트리스트 뒤의 이중 방어다.
// 정상 데이터의 spd 는 137500.0 하나뿐이고, 훼손 데이터는 8.88e20 이다.
inline constexpr double kSpdMin = 0.0;
inline constexpr double kSpdMax = 1.0e9;

// 최대 라인 길이. LineSplitter 의 가드와 같은 값이다.
inline constexpr std::size_t kMaxLineLength = 8192;

// 문법적으로 가능한 최소 길이:
//   타임스탬프 28 + "[0][0][0]" 9 + " BYDA::" 7 + 모듈 1 + ": " 2 = 47
inline constexpr std::size_t kMinLineLength = 47;

struct ParsedLine {
    Module module{};
    std::uint64_t bucket = 0;  // YYYYMMDDHH (예: 2026061922)
    bool has_spd = false;
    bool spd_out_of_range = false;
    double spd = 0.0;

    // 화이트리스트 밖의 모듈이었을 때 그 이름. 어떤 가짜 모듈이
    // 몇 번 나왔는지 리포트하기 위해 보관한다.
    std::string_view module_token;
};

// 라인 하나를 검증하고 필드를 채운다. 예외를 던지지 않는다.
// 핫 패스에서 예외를 쓰면 훼손 라인마다 스택 되감기가 일어나
// 적대적 입력에 대해 처리량이 무너진다.
Reason parse_log_line(std::string_view line, ParsedLine& out) noexcept;

}  // namespace byda

