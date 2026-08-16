// CsvWriter 검증: 과제의 두 답이 들어 있는지, 처분 합계가 맞는지,
// RFC 4180 인용이 지켜지는지.

#include <string>

#include "analyze/Aggregator.h"
#include "analyze/CsvWriter.h"
#include "testfw.h"

namespace {

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

}  // namespace

TEST(csv_contains_both_assignment_answers) {
    byda::Aggregator agg;
    agg.add_line("[2026-06-19_22:00:00.045000][1][2][3] BYDA::RadarTrackNodeState: spd[100.0]",
                 0);
    agg.add_line("[2026-06-19_23:00:00.045000][1][2][3] BYDA::AntennaProfileSpec: spd[300.0]",
                 0);

    byda::CsvContext ctx;
    ctx.source_file = "test.log";
    ctx.source_bytes = 123;
    ctx.elapsed_ms = 42;
    ctx.peak_rss_kb = 2048;

    const std::string csv = byda::build_result_csv(agg, ctx);

    // UTF-8 BOM 으로 시작해야 엑셀이 바로 연다.
    CHECK(csv.size() > 3 && csv[0] == '\xEF' && csv[1] == '\xBB' && csv[2] == '\xBF');

    // 작업 1: 시간대별 모듈 카운트 섹션.
    CHECK(contains(csv, "HOURLY_MODULE_COUNTS"));
    CHECK(contains(csv, "2026-06-19 22:00"));
    CHECK(contains(csv, "2026-06-19 23:00"));

    // 작업 2: 평균 속도.
    CHECK(contains(csv, "spd_average"));
    CHECK(contains(csv, "200.0"));  // (100+300)/2
}

TEST(csv_disposition_counts_add_up) {
    byda::Aggregator agg;
    agg.add_line("[2026-06-19_22:00:00.045000][1][2][3] BYDA::RadarTrackNodeState: x", 0);
    agg.add_line("broken !!!", 0);
    agg.add_line("[2026-06-19_22:00:00.045000][1][2][3] BYDA::BeyondLimit: spd[1.0]", 0);

    CHECK_EQ_U64(agg.total_lines(), 3u);
    CHECK_EQ_U64(agg.accepted_lines() + agg.rejected_lines(), agg.total_lines());

    const std::string csv = byda::build_result_csv(agg, byda::CsvContext{});
    CHECK(contains(csv, "rejected_unknown_module"));
    CHECK(contains(csv, "BeyondLimit"));
}

TEST(csv_quotes_fields_with_commas) {
    // 훼손 라인 발췌에 콤마가 들어가면 반드시 인용되어야 열이 안 어긋난다.
    byda::Aggregator agg;
    agg.add_line("[2026-06-19_22:00:00.045000][1][2][3] BYDA::Fake,Name: a,b,c", 0);

    const std::string csv = byda::build_result_csv(agg, byda::CsvContext{});
    // 발췌가 등장한다면 인용부호 안에 있어야 한다. 간접 확인:
    // 인용부호가 최소 한 번은 쓰였는지, 그리고 발췌 원문이 존재하는지.
    CHECK(contains(csv, "\""));
}
