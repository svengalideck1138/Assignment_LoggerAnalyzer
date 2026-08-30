// FrameDecoder / Protocol 검증: 헤더 왕복, 조각 분할, 조작된 헤더 방어.
//
// 과제 평가 기준 P2(네트워크 견고성)의 단위 증명이다. 잘린 헤더,
// 틀린 magic, 조작된 거대 길이가 와도 크래시 없이 명확한 에러로
// 끝나는지 확인한다.

#include <cstring>
#include <vector>

#include "net/FrameDecoder.h"
#include "net/Protocol.h"
#include "testfw.h"

using byda::ErrorCode;
using byda::FrameDecoder;
using byda::FrameHeader;
using byda::MsgType;

namespace {

std::vector<std::uint8_t> make_frame(MsgType type, const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> out;
    byda::serialize_frame(type, payload, out);
    return out;
}

}  // namespace

TEST(header_encode_decode_roundtrip) {
    FrameHeader h;
    h.type = MsgType::UploadChunk;
    h.flags = 0x1234;
    h.payload_len = 0x0102030405060708ull;

    byda::HeaderBytes raw{};
    byda::encode_header(h, raw);

    // 빅엔디안 골든 벡터: magic 'B','Y','D','A'.
    CHECK(raw[0] == 0x42 && raw[1] == 0x59 && raw[2] == 0x44 && raw[3] == 0x41);
    CHECK(raw[4] == byda::kVersion);
    CHECK(raw[5] == static_cast<std::uint8_t>(MsgType::UploadChunk));
    CHECK(raw[8] == 0x01 && raw[15] == 0x08);  // 길이의 최상위/최하위 바이트

    FrameHeader back;
    byda::decode_header(raw, back);
    CHECK(back.magic == byda::kMagic);
    CHECK(back.type == MsgType::UploadChunk);
    CHECK(back.flags == 0x1234);
    CHECK(back.payload_len == h.payload_len);
}

TEST(decodes_whole_frame) {
    const std::vector<std::uint8_t> payload = {1, 2, 3, 4, 5};
    const std::vector<std::uint8_t> wire = make_frame(MsgType::Hello, payload);

    FrameDecoder dec;
    std::size_t used = 0;
    const auto st = dec.feed(reinterpret_cast<const char*>(wire.data()), wire.size(), used);
    CHECK(st == FrameDecoder::Status::Ready);
    CHECK_EQ_U64(used, wire.size());
    CHECK(dec.header().type == MsgType::Hello);
    CHECK(dec.payload() == payload);
}

TEST(decodes_frame_fed_one_byte_at_a_time) {
    // 네트워크 조각은 경계와 무관하다: 1바이트씩 흘려도 같은 결과.
    const std::vector<std::uint8_t> payload = {9, 8, 7};
    const std::vector<std::uint8_t> wire = make_frame(MsgType::UploadEnd, payload);

    FrameDecoder dec;
    FrameDecoder::Status st = FrameDecoder::Status::NeedMore;
    for (std::size_t i = 0; i < wire.size(); ++i) {
        std::size_t used = 0;
        st = dec.feed(reinterpret_cast<const char*>(wire.data() + i), 1, used);
        CHECK_EQ_U64(used, 1u);
        if (i + 1 < wire.size()) {
            CHECK(st == FrameDecoder::Status::NeedMore);
        }
    }
    CHECK(st == FrameDecoder::Status::Ready);
    CHECK(dec.header().type == MsgType::UploadEnd);
    CHECK(dec.payload() == payload);
}

TEST(decodes_two_frames_in_one_fragment) {
    std::vector<std::uint8_t> wire = make_frame(MsgType::Hello, {1});
    const std::vector<std::uint8_t> second = make_frame(MsgType::Bye, {});
    wire.insert(wire.end(), second.begin(), second.end());

    FrameDecoder dec;
    std::size_t off = 0, used = 0;

    CHECK(dec.feed(reinterpret_cast<const char*>(wire.data()), wire.size(), used) ==
          FrameDecoder::Status::Ready);
    CHECK(dec.header().type == MsgType::Hello);
    off += used;
    dec.reset_frame();

    CHECK(dec.feed(reinterpret_cast<const char*>(wire.data()) + off, wire.size() - off,
                   used) == FrameDecoder::Status::Ready);
    CHECK(dec.header().type == MsgType::Bye);
    CHECK_EQ_U64(off + used, wire.size());
}

TEST(rejects_bad_magic) {
    std::vector<std::uint8_t> wire = make_frame(MsgType::Hello, {});
    wire[0] = 0x00;  // magic 훼손

    FrameDecoder dec;
    std::size_t used = 0;
    CHECK(dec.feed(reinterpret_cast<const char*>(wire.data()), wire.size(), used) ==
          FrameDecoder::Status::Failed);
    CHECK(dec.error() == ErrorCode::BadMagic);
}

TEST(rejects_bad_version) {
    std::vector<std::uint8_t> wire = make_frame(MsgType::Hello, {});
    wire[4] = 0x7F;  // 지원하지 않는 버전

    FrameDecoder dec;
    std::size_t used = 0;
    CHECK(dec.feed(reinterpret_cast<const char*>(wire.data()), wire.size(), used) ==
          FrameDecoder::Status::Failed);
    CHECK(dec.error() == ErrorCode::BadVersion);
}

TEST(rejects_forged_giant_length) {
    // 조작된 길이 하나로 거대한 버퍼를 잡게 만들 수 없어야 한다.
    std::vector<std::uint8_t> wire = make_frame(MsgType::Hello, {});
    // payload_len 을 kMaxPayload 보다 크게 조작한다 (오프셋 8..15, 빅엔디안).
    wire[8] = 0xFF;
    wire[9] = 0xFF;
    wire[10] = 0xFF;
    wire[11] = 0xFF;

    FrameDecoder dec;
    std::size_t used = 0;
    CHECK(dec.feed(reinterpret_cast<const char*>(wire.data()), wire.size(), used) ==
          FrameDecoder::Status::Failed);
    CHECK(dec.error() == ErrorCode::PayloadTooLarge);
}

TEST(truncated_header_waits_for_more) {
    const std::vector<std::uint8_t> wire = make_frame(MsgType::Hello, {1, 2, 3});

    FrameDecoder dec;
    std::size_t used = 0;
    // 헤더 절반만: NeedMore 여야 하고 실패가 아니어야 한다.
    CHECK(dec.feed(reinterpret_cast<const char*>(wire.data()), 8, used) ==
          FrameDecoder::Status::NeedMore);
    CHECK_EQ_U64(used, 8u);
    // 나머지가 오면 정상 완성.
    CHECK(dec.feed(reinterpret_cast<const char*>(wire.data()) + 8, wire.size() - 8, used) ==
          FrameDecoder::Status::Ready);
}

TEST(payload_reader_survives_truncated_payload) {
    // 잘린 payload 를 읽어도 범위 밖 접근 없이 실패로 끝난다.
    byda::PayloadWriter w;
    w.u32(42);
    w.str16("hello");
    std::vector<std::uint8_t> buf = w.bytes();
    buf.resize(buf.size() - 3);  // 문자열 꼬리를 자른다

    byda::PayloadReader r(buf);
    std::uint32_t v = 0;
    CHECK(r.u32(v));
    CHECK_EQ_U64(v, 42u);
    std::string s;
    CHECK(!r.str16(s));  // 실패하되 터지지 않는다
    CHECK(!r.ok());
}
