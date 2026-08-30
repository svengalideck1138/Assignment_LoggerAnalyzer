// Aggregator 검증: 시간대 버킷(작업 1), 평균 속도(작업 2), 거부 통계,
// 그리고 적대적 입력에 대한 컨테이너 상한.

#include <string>

#include "analyze/Aggregator.h"
#include "testfw.h"

namespace {

std::string line_at(const std::string& hour, const std::string& module,
                    const std::string& payload) {
    // hour 예: "2026-06-19_22"
    return "[" + hour + ":00:00.045000][1][2][3] BYDA::" + module + ": " + payload;
}

void add(byda::Aggregator& agg, const std::string& line) {
    agg.add_line(line, 0);
}

}  // namespace

TEST(buckets_group_by_hour) {
    byda::Aggregator agg;
    add(agg, line_at("2026-06-19_22", "RadarTrackNodeState", "spd[100.0]"));
    add(agg, line_at("2026-06-19_22", "AntennaProfileSpec", "x"));
    add(agg, line_at("2026-06-19_23", "RadarTrackNodeState", "spd[200.0]"));

    CHECK_EQ_U64(agg.total_lines(), 3u);
    CHECK_EQ_U64(agg.accepted_lines(), 3u);
    CHECK_EQ_U64(agg.buckets().size(), 2u);

    // 첫 버킷(22시): 모듈별 카운트.
    const auto& b22 = agg.buckets()[0];
    CHECK_EQ_U64(b22.first, 2026061922ull);
    CHECK_EQ_U64(b22.second.total, 2u);
    CHECK_EQ_U64(
        b22.second.module_count[static_cast<std::size_t>(byda::Module::RadarTrackNodeState)],
        1u);

    // 모듈 전체 합계.
    CHECK_EQ_U64(
        agg.module_totals()[static_cast<std::size_t>(byda::Module::RadarTrackNodeState)],
        2u);
}

TEST(spd_average_min_max) {
    byda::Aggregator agg;
    add(agg, line_at("2026-06-19_22", "RadarTrackNodeState", "spd[100.0]"));
    add(agg, line_at("2026-06-19_22", "RadarTrackNodeState", "spd[300.0]"));
    add(agg, line_at("2026-06-19_22", "RadarTrackNodeState", "no speed here"));

    CHECK_EQ_U64(agg.spd_n(), 2u);
    CHECK(agg.spd_avg() == 200.0);
    CHECK(agg.spd_min() == 100.0);
    CHECK(agg.spd_max() == 300.0);
}

TEST(corrupt_lines_are_counted_not_fatal) {
    byda::Aggregator agg;
    add(agg, line_at("2026-06-19_22", "RadarTrackNodeState", "spd[100.0]"));
    add(agg, "totally broken line !!!");
    add(agg, line_at("2026-06-19_22", "BeyondLimit", "spd[888888888888888888888.88]"));
    add(agg, line_at("2026-06-19_22", "RadarTrackNodeState", "spd[200.0]"));

    CHECK_EQ_U64(agg.total_lines(), 4u);
    CHECK_EQ_U64(agg.accepted_lines(), 2u);
    CHECK_EQ_U64(agg.rejected_lines(), 2u);

    // 훼손 라인이 평균을 오염시키지 않았다.
    CHECK(agg.spd_avg() == 150.0);

    // 화이트리스트 밖 모듈명이 리포트용으로 잡혀 있다.
    const auto it = agg.rejected_modules().find("BeyondLimit");
    CHECK(it != agg.rejected_modules().end());
    CHECK_EQ_U64(it->second, 1u);
}

TEST(adversarial_timestamps_cannot_inflate_buckets) {
    // 매 줄 연도가 바뀌는 적대적 입력. 버킷 수는 상한(10,000)에서 멈추고
    // 라인 계수는 계속되어야 한다 (크래시/폭주 없음).
    byda::Aggregator agg;
    for (int year = 0; year < 12000; ++year) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%04d", 1000 + (year % 9000));
        // 연도만 바꾼 정상 라인.
        std::string line = "[";
        line += buf;
        line += "-06-19_22:00:00.045000][1][2][3] BYDA::RadarTrackNodeState: x";
        agg.add_line(line, 0);
    }
    CHECK(agg.buckets().size() <= 10000u);
    CHECK_EQ_U64(agg.total_lines(), 12000u);
}

TEST(reset_clears_everything) {
    byda::Aggregator agg;
    add(agg, line_at("2026-06-19_22", "RadarTrackNodeState", "spd[100.0]"));
    agg.reset();
    CHECK_EQ_U64(agg.total_lines(), 0u);
    CHECK_EQ_U64(agg.buckets().size(), 0u);
    CHECK_EQ_U64(agg.spd_n(), 0u);
}
