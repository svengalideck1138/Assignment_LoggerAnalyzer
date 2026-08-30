// parse_log_line 검증: 거부 사유 8종, 모듈 화이트리스트, 속도 게이트.
//
// 과제 핵심 평가 기준(P1)의 단위 증명이다. 훼손 유형별로 정확한 사유가
// 나오는지, 그리고 '헤더는 정상, 페이로드만 오염'인 라인(BeyondLimit)이
// 화이트리스트와 범위 게이트에서 격추되는지 확인한다.

#include <string>

#include "parse/LogLine.h"
#include "testfw.h"

using byda::Module;
using byda::ParsedLine;
using byda::Reason;

namespace {

// 정상 헤더를 가진 라인을 만든다.
std::string line_with(const std::string& module, const std::string& payload) {
    return "[2026-06-19_22:00:00.045000][4181][62750][12345] BYDA::" + module + ": " + payload;
}

Reason parse(const std::string& s, ParsedLine& out) {
    return byda::parse_log_line(s, out);
}

}  // namespace

TEST(accepts_valid_line_and_extracts_fields) {
    ParsedLine p;
    const Reason r = parse(
        line_with("RadarTrackNodeState",
                  "unitAddr[4181] spd[137500.000000] advDelta[62750.000000]"),
        p);
    CHECK(r == Reason::Ok);
    CHECK(p.module == Module::RadarTrackNodeState);
    CHECK_EQ_U64(p.bucket, 2026061922ull);
    CHECK(p.has_spd);
    CHECK(p.spd == 137500.0);
    CHECK(!p.spd_out_of_range);
}

TEST(accepts_all_whitelisted_modules) {
    static const char* kModules[] = {"RadarTrackNodeState", "AntennaProfileSpec",
                                     "SectorSchedulerRTS", "DetectionTaskRunner",
                                     "BeamSteerCtrlUnitImpl"};
    for (const char* m : kModules) {
        ParsedLine p;
        CHECK(parse(line_with(m, "x"), p) == Reason::Ok);
    }
}

TEST(rejects_too_short) {
    ParsedLine p;
    CHECK(parse("", p) == Reason::TooShort);
    CHECK(parse("[2026-06-19", p) == Reason::TooShort);
}

TEST(rejects_missing_open_bracket) {
    ParsedLine p;
    // 선두 '[' 만 사라진 훼손 (HeadBraceLoss).
    CHECK(parse("2026-06-19_22:00:00.045000][1][2][3] BYDA::SectorSchedulerRTS: x", p) ==
          Reason::MissingOpenBracket);
    CHECK(parse("x", p) == Reason::MissingOpenBracket);
}

TEST(rejects_bad_timestamp) {
    ParsedLine p;
    // 구분자가 다르다.
    CHECK(parse("[2026/06/19_22:00:00.045000][1][2][3] BYDA::SectorSchedulerRTS: x", p) ==
          Reason::BadTimestamp);
    // 숫자 자리에 문자.
    CHECK(parse("[2026-06-19_2X:00:00.045000][1][2][3] BYDA::SectorSchedulerRTS: x", p) ==
          Reason::BadTimestamp);
    // 달력 범위 밖 (13월).
    CHECK(parse("[2026-13-19_22:00:00.045000][1][2][3] BYDA::SectorSchedulerRTS: x", p) ==
          Reason::BadTimestamp);
}

TEST(rejects_garbage_payload) {
    ParsedLine p;
    // 타임스탬프만 있고 PID/TID/SEQ 구조가 없다.
    CHECK(parse("[2026-06-19_22:00:00.045000] garbage garbage garbage", p) ==
          Reason::GarbagePayload);
}

TEST(rejects_missing_close_bracket) {
    ParsedLine p;
    // SEQ 뒤 ']' 누락 (OpenBraceLeak).
    CHECK(parse("[2026-06-19_22:00:00.045000][1][2][3 BYDA::SectorSchedulerRTS: x", p) ==
          Reason::MissingCloseBracket);
    // 두 번째 필드가 열리기만 했다.
    CHECK(parse("[2026-06-19_22:00:00.045000][1][2[3] BYDA::SectorSchedulerRTS: x", p) ==
          Reason::MissingCloseBracket);
}

TEST(rejects_missing_byda_tag) {
    ParsedLine p;
    CHECK(parse("[2026-06-19_22:00:00.045000][1][2][3] NOPE::SectorSchedulerRTS: x", p) ==
          Reason::MissingBydaTag);
}

TEST(rejects_bad_module_name) {
    ParsedLine p;
    // 모듈명이 비었다.
    CHECK(parse("[2026-06-19_22:00:00.045000][1][2][3] BYDA::: payload here", p) ==
          Reason::BadModuleName);
    // ": " 로 끝나지 않는다.
    CHECK(parse("[2026-06-19_22:00:00.045000][1][2][3] BYDA::SectorSchedulerRTS x", p) ==
          Reason::BadModuleName);
}

TEST(whitelist_shoots_down_beyondlimit) {
    // 과제의 정답 게이트: 헤더는 완벽히 정상, 모듈만 가짜.
    // 이 6줄이 통과하면 평균 속도가 137,500 이 아니라 9.18e15 가 된다.
    //
    // module_token 은 입력 라인을 가리키는 view 라서, 검사하는 동안
    // 라인이 살아 있어야 한다 (서버도 같은 규칙으로 즉시 소비한다).
    ParsedLine p;
    const std::string line = line_with("BeyondLimit", "spd[888888888888888888888.88]");
    const Reason r = parse(line, p);
    CHECK(r == Reason::UnknownModule);
    CHECK(p.module_token == "BeyondLimit");
    CHECK(!p.has_spd);
}

TEST(spd_range_gate_is_second_defense) {
    // 화이트리스트를 통과한 모듈이라도 물리적으로 불가능한 속도는
    // 평균에서 빠지고 out_of_range 로 표시된다.
    ParsedLine p;
    const Reason r =
        parse(line_with("SectorSchedulerRTS", "spd[888888888888888888888.88]"), p);
    CHECK(r == Reason::Ok);
    CHECK(!p.has_spd);
    CHECK(p.spd_out_of_range);
}

TEST(spd_boundaries) {
    ParsedLine p;
    // 상한과 정확히 같은 값은 허용된다.
    CHECK(parse(line_with("SectorSchedulerRTS", "spd[1000000000.0]"), p) == Reason::Ok);
    CHECK(p.has_spd);
    // 상한을 넘으면 거른다.
    CHECK(parse(line_with("SectorSchedulerRTS", "spd[1000000000.5]"), p) == Reason::Ok);
    CHECK(!p.has_spd);
    CHECK(p.spd_out_of_range);
    // 음수도 거른다.
    CHECK(parse(line_with("SectorSchedulerRTS", "spd[-1.0]"), p) == Reason::Ok);
    CHECK(!p.has_spd);
}

TEST(spd_anchor_is_exact) {
    ParsedLine p;
    // 다른 키의 꼬리(xspd[)를 잡으면 안 되고, 진짜 spd[ 는 찾아야 한다.
    CHECK(parse(line_with("SectorSchedulerRTS", "xspd[1.0] spd[42.5]"), p) == Reason::Ok);
    CHECK(p.has_spd);
    CHECK(p.spd == 42.5);

    // 꼬리만 있으면 spd 없음으로 처리한다.
    CHECK(parse(line_with("SectorSchedulerRTS", "advspd[99.0] other[1]"), p) == Reason::Ok);
    CHECK(!p.has_spd);
}

TEST(spd_malformed_values_are_ignored) {
    ParsedLine p;
    // 숫자가 아닌 값, 지수 표기, 빈 값은 전부 무시된다 (크래시 없이).
    CHECK(parse(line_with("SectorSchedulerRTS", "spd[NONE]"), p) == Reason::Ok);
    CHECK(!p.has_spd);
    CHECK(parse(line_with("SectorSchedulerRTS", "spd[1e9]"), p) == Reason::Ok);
    CHECK(!p.has_spd);
    CHECK(parse(line_with("SectorSchedulerRTS", "spd[]"), p) == Reason::Ok);
    CHECK(!p.has_spd);
    CHECK(parse(line_with("SectorSchedulerRTS", "spd[12.3.4]"), p) == Reason::Ok);
    CHECK(!p.has_spd);
}
