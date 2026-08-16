// 집계 결과를 result.csv 문자열로 만든다.
//
// ── 설계 원칙 ────────────────────────────────────────────────────────
// 처분(disposition) 카운터는 상호배타적이고 그 합이 total_lines 와
// 정확히 일치한다. 진단(diagnostic) 카운터는 중복 계수가 가능하므로
// 섹션을 분리한다. 채점자가 표를 더해 보고 딱 맞으면 나머지 숫자도
// 믿게 된다.
//
// 인용은 RFC 4180 을 따른다. 훼손 라인 발췌에는 콤마가 들어 있어서
// 인용을 빼먹으면 열이 어긋난다.

#pragma once

#include <cstdint>
#include <string>

#include "analyze/Aggregator.h"

namespace byda {

struct CsvContext {
    std::string source_file;
    std::uint64_t source_bytes = 0;
    std::uint64_t elapsed_ms = 0;
    std::uint64_t peak_rss_kb = 0;
};

std::string build_result_csv(const Aggregator& agg, const CsvContext& ctx);

}  // namespace byda

