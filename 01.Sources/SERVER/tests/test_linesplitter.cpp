// LineSplitter 검증: 청크 분할 불변성이 핵심이다.
//
// 네트워크 조각은 라인 경계와 무관하므로, "어떤 크기로 쪼개 넣어도
// 결과 라인은 동일하다"가 이 클래스의 존재 이유다. 그 성질을 그대로
// 테스트한다. 초장문 라인 가드(메모리 제약 방어)도 함께 확인한다.

#include <string>
#include <vector>

#include "parse/LineSplitter.h"
#include "testfw.h"

namespace {

// 주어진 청크 크기로 쪼개 넣고 나온 라인들을 모은다.
std::vector<std::string> split_with_chunk(const std::string& input, std::size_t chunk,
                                          std::size_t max_line = 8192) {
    byda::LineSplitter sp(max_line);
    std::vector<std::string> out;
    for (std::size_t pos = 0; pos < input.size(); pos += chunk) {
        const std::size_t n = pos + chunk <= input.size() ? chunk : input.size() - pos;
        sp.feed(input.data() + pos, n,
                [&](std::string_view line, std::size_t) { out.emplace_back(line); });
    }
    sp.finish([&](std::string_view line, std::size_t) { out.emplace_back(line); });
    return out;
}

}  // namespace

TEST(chunk_split_invariance) {
    // 짧은 줄, 긴 줄, 빈 줄, CRLF 줄이 섞인 입력.
    const std::string input =
        "first line\n"
        "\n"
        "a much longer second line with several words in it\n"
        "crlf line\r\n"
        "tail without newline";

    const std::vector<std::string> expected =
        split_with_chunk(input, input.size());  // 통짜 기준

    CHECK_EQ_U64(expected.size(), 5u);
    CHECK(expected[0] == "first line");
    CHECK(expected[1] == "");
    CHECK(expected[3] == "crlf line");        // CR 이 떨어져 있어야 한다
    CHECK(expected[4] == "tail without newline");

    // 1바이트부터 어중간한 소수 크기까지, 어떤 분할에서도 같은 결과.
    for (const std::size_t chunk : {std::size_t{1}, std::size_t{2}, std::size_t{3},
                                    std::size_t{7}, std::size_t{13}, std::size_t{64}}) {
        const std::vector<std::string> got = split_with_chunk(input, chunk);
        CHECK(got == expected);
    }
}

TEST(raw_bytes_cover_the_whole_stream) {
    // raw_bytes(개행/CR 포함)의 합이 입력 전체 크기와 일치해야
    // 호출자가 스트림 오프셋을 정확히 추적할 수 있다.
    const std::string input = "aa\nbb\r\ncc\n";
    byda::LineSplitter sp;
    std::size_t total = 0;
    sp.feed(input.data(), input.size(),
            [&](std::string_view, std::size_t raw) { total += raw; });
    sp.finish([&](std::string_view, std::size_t raw) { total += raw; });
    CHECK_EQ_U64(total, input.size());
    CHECK_EQ_U64(sp.lines(), 3u);
}

TEST(carry_joins_lines_across_fragments) {
    byda::LineSplitter sp;
    std::vector<std::string> out;
    const auto sink = [&](std::string_view line, std::size_t) { out.emplace_back(line); };

    sp.feed("abc", 3, sink);
    CHECK_EQ_U64(out.size(), 0u);  // 아직 개행이 없다
    sp.feed("def\n", 4, sink);
    CHECK_EQ_U64(out.size(), 1u);
    CHECK(out[0] == "abcdef");
}

TEST(oversize_line_is_dropped_not_accumulated) {
    // 개행 없는 폭주 스트림이 carry 를 무한히 키우면 메모리 제약을
    // 정면으로 위반한다. max_line 을 넘으면 그 라인은 버려야 한다.
    byda::LineSplitter sp(16);
    std::vector<std::string> out;
    const auto sink = [&](std::string_view line, std::size_t) { out.emplace_back(line); };

    const std::string big(100, 'x');
    sp.feed(big.data(), big.size(), sink);
    sp.feed("\nok\n", 4, sink);

    CHECK_EQ_U64(sp.oversize(), 1u);
    CHECK_EQ_U64(out.size(), 1u);
    CHECK(out[0] == "ok");                    // 다음 라인부터 정상 동작
    CHECK(sp.carry_size() <= 16u);            // carry 가 자라 있지 않다
}

TEST(oversize_across_fragments) {
    // 조각 여러 개에 걸쳐 자라는 초장문도 같은 방식으로 버려진다.
    byda::LineSplitter sp(16);
    std::vector<std::string> out;
    const auto sink = [&](std::string_view line, std::size_t) { out.emplace_back(line); };

    for (int i = 0; i < 10; ++i) {
        sp.feed("xxxxxxxx", 8, sink);         // 개행 없이 80바이트
    }
    sp.feed("\nnext\n", 6, sink);
    CHECK_EQ_U64(sp.oversize(), 1u);
    CHECK_EQ_U64(out.size(), 1u);
    CHECK(out[0] == "next");
}
