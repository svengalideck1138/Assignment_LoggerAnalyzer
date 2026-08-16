// 파싱 결과를 시간대별로 모은다.
//
// ── 컨테이너 선택 ────────────────────────────────────────────────────
// std::map 이 아니라 정렬된 std::vector + 직전 버킷 캐시를 쓴다.
// 로그가 시간순이므로 연속된 라인의 거의 전부가 직전과 같은 버킷에
// 들어간다. 캐시 히트면 비교 한 번으로 끝나고, 미스일 때만
// lower_bound 로 자리를 찾는다.
//
// map 을 쓰면 항목마다 노드 할당이 일어난다. vector 는 25개 버킷이
// 연속된 1.2KB 메모리(버킷당 48바이트)에 들어가고, 할당은 사실상
// 처음 한 번뿐이다.
//
// ── 부동소수점 합 ────────────────────────────────────────────────────
// 이 데이터에서는 580,661 x 137,500 = 79,840,887,500 < 2^53 이라
// double 로도 오차가 0이다. 그래도 Neumaier 보상합을 쓴다.
// 비용은 덧셈 세 번이고, 다른 입력에 대해서도 결과가 안정적이다.

#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "parse/LogLine.h"

namespace byda {

// 시간 버킷 하나의 집계.
//
// 작업 1(모듈 발생 횟수를 시간대별로)이 요구하는 것만 담는다.
// 속도는 작업 2 가 전체 평균 하나만 요구하므로 버킷에 두지 않고
// Aggregator 의 전역 누적기로만 관리한다.
struct BucketStat {
    std::array<std::uint64_t, kModuleCount> module_count{};
    std::uint64_t total = 0;
};

struct CorruptSample {
    std::uint64_t count = 0;
    std::uint64_t first_line_no = 0;
    std::uint64_t first_byte_offset = 0;
    std::string excerpt;  // 사유별 첫 사례 원문 (이스케이프 + 길이 제한)
};

class Aggregator {
public:
    Aggregator();

    void reset();

    // 라인 하나를 넣는다. byte_offset 은 스트림 시작 기준 이 라인의 위치.
    void add_line(std::string_view line, std::uint64_t byte_offset);

    // LineSplitter 가 버린 초장문 라인 수를 반영한다.
    void add_oversize(std::uint64_t n) noexcept { oversize_lines_ += n; }

    // ---- 조회 ----
    std::uint64_t total_lines() const noexcept { return total_lines_; }
    std::uint64_t accepted_lines() const noexcept { return accepted_lines_; }
    std::uint64_t rejected_lines() const noexcept { return total_lines_ - accepted_lines_; }
    std::uint64_t oversize_lines() const noexcept { return oversize_lines_; }
    std::uint64_t spd_out_of_range() const noexcept { return spd_out_of_range_; }

    const std::array<std::uint64_t, kModuleCount>& module_totals() const noexcept {
        return module_totals_;
    }

    std::uint64_t spd_n() const noexcept { return spd_n_; }
    double spd_sum() const noexcept { return spd_sum_ + spd_comp_; }
    double spd_avg() const noexcept {
        return spd_n_ > 0 ? spd_sum() / static_cast<double>(spd_n_) : 0.0;
    }
    double spd_min() const noexcept { return spd_min_; }
    double spd_max() const noexcept { return spd_max_; }

    const std::vector<std::pair<std::uint64_t, BucketStat>>& buckets() const noexcept {
        return buckets_;
    }

    const std::array<CorruptSample, kReasonCount>& corrupt() const noexcept { return corrupt_; }

    const std::map<std::string, std::uint64_t>& rejected_modules() const noexcept {
        return rejected_modules_;
    }

    const std::string& first_timestamp() const noexcept { return first_ts_; }
    const std::string& last_timestamp() const noexcept { return last_ts_; }

private:
    BucketStat& bucket_for(std::uint64_t key);
    void record_reject(Reason r, std::string_view line, std::uint64_t byte_offset,
                       std::string_view module_token);

    // 적대적 타임스탬프(매 줄 연도를 바꾸는)로 컨테이너를 부풀리는 것을 막는다.
    static constexpr std::size_t kMaxBuckets = 10000;
    // 가짜 모듈명이 무한히 쌓이지 않도록 상한을 둔다.
    static constexpr std::size_t kMaxRejectedModules = 64;

    std::uint64_t total_lines_ = 0;
    std::uint64_t accepted_lines_ = 0;
    std::uint64_t oversize_lines_ = 0;
    std::uint64_t spd_out_of_range_ = 0;

    std::array<std::uint64_t, kModuleCount> module_totals_{};

    std::uint64_t spd_n_ = 0;
    double spd_sum_ = 0.0;
    double spd_comp_ = 0.0;
    double spd_min_ = 0.0;
    double spd_max_ = 0.0;

    std::vector<std::pair<std::uint64_t, BucketStat>> buckets_;
    std::uint64_t last_key_ = 0;
    std::size_t last_index_ = 0;
    bool has_last_ = false;

    std::array<CorruptSample, kReasonCount> corrupt_{};
    std::map<std::string, std::uint64_t> rejected_modules_;

    std::string first_ts_;
    std::string last_ts_;
};

// 버킷 키(YYYYMMDDHH)를 "YYYY-MM-DD HH:00" 으로.
std::string format_bucket(std::uint64_t key);

}  // namespace byda

