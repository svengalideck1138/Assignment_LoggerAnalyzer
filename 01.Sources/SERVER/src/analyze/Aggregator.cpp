#include "analyze/Aggregator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace byda {
namespace {

// Neumaier 보상합. 단순 누적보다 오차가 작고, 큰 값과 작은 값이
// 섞여 들어와도 결과가 안정적이다.
inline void neumaier_add(double& sum, double& comp, double v) noexcept {
    const double t = sum + v;
    if (std::fabs(sum) >= std::fabs(v)) {
        comp += (sum - t) + v;
    } else {
        comp += (v - t) + sum;
    }
    sum = t;
}

// 로그 원문을 CSV 에 실을 때 제어문자가 그대로 들어가면 파일이 깨진다.
// 비출력 바이트는 \xNN 으로 바꾸고 길이도 제한한다.
std::string escape_excerpt(std::string_view line, std::size_t max_len = 160) {
    std::string out;
    out.reserve(std::min(line.size(), max_len) + 8);

    for (std::size_t i = 0; i < line.size() && out.size() < max_len; ++i) {
        const unsigned char c = static_cast<unsigned char>(line[i]);
        if (c >= 0x20 && c < 0x7F) {
            out.push_back(static_cast<char>(c));
        } else {
            std::array<char, 8> buf{};
            std::snprintf(buf.data(), buf.size(), "\\x%02X", c);
            out.append(buf.data());
        }
    }
    if (line.size() > max_len) {
        out.append("...");
    }
    return out;
}

}  // namespace

Aggregator::Aggregator() { buckets_.reserve(32); }

void Aggregator::reset() {
    total_lines_ = 0;
    accepted_lines_ = 0;
    oversize_lines_ = 0;
    spd_out_of_range_ = 0;
    module_totals_.fill(0);

    spd_n_ = 0;
    spd_sum_ = 0.0;
    spd_comp_ = 0.0;
    spd_min_ = 0.0;
    spd_max_ = 0.0;

    buckets_.clear();  // capacity 유지
    last_key_ = 0;
    last_index_ = 0;
    has_last_ = false;

    for (auto& c : corrupt_) {
        c = CorruptSample{};
    }
    rejected_modules_.clear();
    first_ts_.clear();
    last_ts_.clear();
}

BucketStat& Aggregator::bucket_for(std::uint64_t key) {
    // 로그가 시간순이라 거의 항상 여기서 끝난다.
    if (has_last_ && last_key_ == key) {
        return buckets_[last_index_].second;
    }

    auto it = std::lower_bound(
        buckets_.begin(), buckets_.end(), key,
        [](const std::pair<std::uint64_t, BucketStat>& e, std::uint64_t k) {
            return e.first < k;
        });

    if (it != buckets_.end() && it->first == key) {
        last_key_ = key;
        last_index_ = static_cast<std::size_t>(it - buckets_.begin());
        has_last_ = true;
        return it->second;
    }

    it = buckets_.emplace(it, key, BucketStat{});
    last_key_ = key;
    last_index_ = static_cast<std::size_t>(it - buckets_.begin());
    has_last_ = true;
    return it->second;
}

void Aggregator::record_reject(Reason r, std::string_view line, std::uint64_t byte_offset,
                               std::string_view module_token) {
    const std::size_t idx = static_cast<std::size_t>(r);
    if (idx >= kReasonCount) {
        return;
    }

    CorruptSample& c = corrupt_[idx];
    if (c.count == 0) {
        // 사유별 첫 사례만 원문을 보관한다. 무제한으로 모으면
        // 적대적인 500MB 입력이 그대로 메모리 폭탄이 된다.
        c.first_line_no = total_lines_;
        c.first_byte_offset = byte_offset;
        c.excerpt = escape_excerpt(line);
    }
    ++c.count;

    if (r == Reason::UnknownModule && !module_token.empty()) {
        if (rejected_modules_.size() < kMaxRejectedModules) {
            ++rejected_modules_[std::string(module_token)];
        } else {
            auto it = rejected_modules_.find(std::string(module_token));
            if (it != rejected_modules_.end()) {
                ++it->second;
            }
        }
    }
}

void Aggregator::add_line(std::string_view line, std::uint64_t byte_offset) {
    ++total_lines_;

    ParsedLine p;
    const Reason r = parse_log_line(line, p);

    if (r != Reason::Ok) {
        record_reject(r, line, byte_offset, p.module_token);
        return;
    }

    ++accepted_lines_;

    // 타임스탬프 원문(대괄호 안쪽 26자)을 시간 범위 표시용으로 남긴다.
    if (first_ts_.empty()) {
        first_ts_.assign(line.data() + 1, 26);
    }
    last_ts_.assign(line.data() + 1, 26);

    if (p.spd_out_of_range) {
        ++spd_out_of_range_;
    }

    const std::size_t mi = static_cast<std::size_t>(p.module);
    ++module_totals_[mi];

    // 버킷 상한 초과 시에는 전역 통계만 유지하고 새 버킷을 만들지 않는다.
    if (buckets_.size() >= kMaxBuckets) {
        // binary_search 는 비교자를 (값, 원소) 순서로도 호출하므로
        // 여기서는 lower_bound 로 존재 여부만 확인한다.
        auto found = std::lower_bound(
            buckets_.begin(), buckets_.end(), p.bucket,
            [](const std::pair<std::uint64_t, BucketStat>& e, std::uint64_t k) {
                return e.first < k;
            });
        const bool exists = (found != buckets_.end() && found->first == p.bucket);
        if (!exists) {
            if (p.has_spd) {
                ++spd_n_;
                neumaier_add(spd_sum_, spd_comp_, p.spd);
            }
            return;
        }
    }

    BucketStat& b = bucket_for(p.bucket);
    ++b.module_count[mi];
    ++b.total;

    // 속도는 전체 평균 하나만 필요하므로 전역 누적기에만 더한다.
    if (p.has_spd) {
        ++spd_n_;
        neumaier_add(spd_sum_, spd_comp_, p.spd);
        if (spd_n_ == 1) {
            spd_min_ = p.spd;
            spd_max_ = p.spd;
        } else {
            if (p.spd < spd_min_) spd_min_ = p.spd;
            if (p.spd > spd_max_) spd_max_ = p.spd;
        }
    }
}

std::string format_bucket(std::uint64_t key) {
    const unsigned hour = static_cast<unsigned>(key % 100ull);
    const unsigned day = static_cast<unsigned>((key / 100ull) % 100ull);
    const unsigned month = static_cast<unsigned>((key / 10000ull) % 100ull);
    const unsigned year = static_cast<unsigned>(key / 1000000ull);

    std::array<char, 32> buf{};
    std::snprintf(buf.data(), buf.size(), "%04u-%02u-%02u %02u:00", year, month, day, hour);
    return std::string(buf.data());
}

}  // namespace byda
