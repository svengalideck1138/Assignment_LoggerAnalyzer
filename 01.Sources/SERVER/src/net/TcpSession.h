// 클라이언트 연결 하나를 담당하는 세션.
//
// ── 수동 메모리 관리 금지 규칙을 libuv 위에서 지키는 방법 ──────────────
//
// libuv 의 표준 예제는 연결마다 uv_tcp_t 를 C 표준 할당 함수로 잡았다가
// 닫힘 콜백에서 반납하고, 읽기 콜백이 불릴 때마다 수신 버퍼도 새로 잡는다.
// 이 과제가 금지한 방식이다. 대신 다음과 같이 구성한다:
//
//   1) 핸들(uv_tcp_t, uv_timer_t)을 이 클래스의 '멤버 변수'로 둔다.
//      핸들을 위한 할당 자체가 존재하지 않는다.
//   2) 세션 객체는 TcpServer 가 std::unique_ptr 로 소유한다.
//      uv_close 콜백이 모두 끝난 시점에 컨테이너에서 지우면 소멸자가 돈다.
//   3) alloc 콜백은 생성 시 한 번 잡아 둔 고정 크기 std::vector<char> 의
//      포인터를 그대로 돌려준다. 읽기마다 일어나는 할당이 0이 된다.
//   4) uv_write 는 완료 콜백까지 버퍼가 살아 있어야 하므로, 직렬화된
//      바이트열을 std::deque<WriteOp> 에 담아 세션이 소유한다.
//      deque 는 push_back/pop_front 시 기존 원소의 주소가 유지되므로
//      uv_write_t 포인터가 안전하다.
//
// ── 500MB 를 메모리에 올리지 않는 방법 ────────────────────────────────
//
// UPLOAD_CHUNK 의 payload 는 IPayloadSink 를 통해 수신 버퍼에서 곧바로
// LineSplitter 로 흘러간다. 프레임 버퍼에 모으지 않으므로, 파일이 아무리
// 커도 세션이 붙잡는 메모리는 고정된 수신 버퍼(256KiB) + carry(8KiB)뿐이다.

#pragma once

#include <uv.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "analyze/Aggregator.h"
#include "app/Config.h"
#include "net/FrameDecoder.h"
#include "net/IPayloadSink.h"
#include "net/ISessionCallback.h"
#include "net/Protocol.h"
#include "parse/LineSplitter.h"

namespace byda {

class TcpSession : public IPayloadSink {
public:
    // 소유자와는 ISessionCallback 으로만 대화한다. 설정은 읽기 전용
    // 참조로 받는다 (소유자가 세션보다 오래 살므로 안전하다).
    TcpSession(ISessionCallback& callback, const Config& cfg, std::uint32_t id) noexcept;
    ~TcpSession() override;

    TcpSession(const TcpSession&) = delete;
    TcpSession& operator=(const TcpSession&) = delete;

    // 핸들 초기화. uv_accept 보다 먼저 호출해야 한다.
    bool init(uv_loop_t* loop, std::size_t read_buffer_bytes, int idle_timeout_ms,
              int upload_stall_timeout_ms);

    uv_tcp_t* tcp() noexcept { return &tcp_; }
    std::uint32_t id() const noexcept { return id_; }
    const std::string& peer() const noexcept { return peer_; }

    // accept 직후 호출. 읽기를 시작하고 유휴 타이머를 건다.
    bool start();

    // 연결 정리. 여러 번 불려도 안전하다.
    void close(const std::string& reason);

    // 접속 상한 초과 등으로 즉시 거절할 때.
    void reject(ErrorCode code, const std::string& message);

    // IPayloadSink
    bool stream_payload(MsgType type, std::uint64_t length) override;
    void payload_chunk(const char* data, std::size_t len) override;

private:
    enum class State {
        AwaitHello,
        Ready,
        Uploading,
        Closing,
    };

    struct WriteOp {
        uv_write_t req{};
        std::vector<std::uint8_t> data;
        TcpSession* owner = nullptr;
    };

    static void on_alloc(uv_handle_t* h, std::size_t suggested, uv_buf_t* buf);
    static void on_read(uv_stream_t* s, ssize_t nread, const uv_buf_t* buf);
    static void on_write(uv_write_t* req, int status);
    static void on_handle_closed(uv_handle_t* h);
    static void on_idle_timeout(uv_timer_t* t);

    void process(const char* data, std::size_t len);
    bool dispatch_frame();
    bool handle_hello();
    bool handle_upload_begin();
    bool handle_upload_end();
    void send_result_csv();
    void abort_upload(ErrorCode code, const std::string& message);

    // 업로드가 정상적으로 끝나지 못했을 때, 그 시점까지의 처리 결과와
    // 메모리 사용량을 로그로 남기고 요약 문자열을 돌려준다.
    std::string report_partial_upload(const char* headline);

    void send(MsgType type, const std::vector<std::uint8_t>& payload);
    void send_error(ErrorCode code, const std::string& message);
    void touch_idle();
    void resolve_peer();

    ISessionCallback& callback_;
    const Config& config_;
    std::uint32_t id_ = 0;

    uv_tcp_t tcp_{};
    uv_timer_t idle_timer_{};
    bool handles_inited_ = false;
    int pending_close_ = 0;

    std::vector<char> read_buf_;
    FrameDecoder decoder_;
    std::deque<WriteOp> writes_;

    std::string peer_ = "unknown";
    std::string client_name_;
    State state_ = State::AwaitHello;
    bool close_after_flush_ = false;
    int idle_timeout_ms_ = 300000;
    int upload_stall_timeout_ms_ = 15000;
    int stall_ticks_ = 0;   // 데이터 없이 지나간 정체 감시 구간 수

    std::uint64_t rx_bytes_ = 0;
    std::uint64_t rx_frames_ = 0;

    // ---- 업로드 / 분석 진행 상태 ----
    LineSplitter splitter_;
    Aggregator agg_;
    std::uint32_t upload_id_ = 0;
    std::string upload_name_;
    std::uint64_t upload_declared_ = 0;   // 클라이언트가 알려온 총 바이트
    std::uint64_t upload_received_ = 0;   // 지금까지 실제로 받아 파싱한 바이트
    std::uint64_t last_ack_at_ = 0;       // 마지막으로 UPLOAD_ACK 을 보낸 시점의 수신량
    std::uint64_t upload_start_ns_ = 0;
    std::uint64_t line_offset_ = 0;       // 스트림 내 현재 라인의 시작 오프셋
    std::string result_csv_;              // 완성된 result.csv (약 5KB)
};

}  // namespace byda

