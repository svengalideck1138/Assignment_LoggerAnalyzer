// Zhenyu_LoggerAnalyzer C++ client - 전송 엔진 (워커 스레드).
//
// 과제 요구사항 C2: 파일 전송은 반드시 워커 스레드로 처리하고,
// 500MB 전송 중에도 UI 스레드가 멈추면 안 된다.
//
// 구조:
//   - UI 스레드는 start_*() 로 작업을 시작하고, 매 프레임 snapshot() 으로
//     진행 상황의 복사본을 읽어 그린다. 워커가 갱신 중인 객체를 직접
//     보여주지 않으므로 찢어진 값이 화면에 나오지 않는다.
//   - 취소는 원자 플래그 하나다. 워커는 청크 사이와 poll 슬라이스마다
//     플래그를 보고, C# 클라이언트와 같은 순서로 CANCEL 을 보낸 뒤
//     서버의 ERROR(Cancelled) 응답을 잠깐 기다린다.
//   - 한 번에 하나의 작업만 실행된다. 이전 작업이 끝나기 전의 start 요청은
//     무시된다.
//
// 와이어 규약은 서버의 net/Protocol.h 를 그대로 include 한다.

#pragma once

#include <net/Protocol.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Socket.hpp"

namespace bydacli {

enum class Phase {
    Idle,
    Connecting,
    Handshaking,
    Uploading,
    WaitingAnalysis,
    ReceivingResult,
    Done,
    Cancelled,
    Failed,
};

const char* phase_text(Phase p) noexcept;

// HELLO_ACK 내용.
struct HelloInfo {
    std::uint32_t session_id = 0;
    std::uint32_t max_chunk = 0;
    std::string observed_peer;
    std::string server_version;
};

// UI 가 매 프레임 복사해 가는 진행 상황.
struct Snapshot {
    Phase phase = Phase::Idle;
    std::string error;  // Failed 일 때의 사유

    HelloInfo hello;
    bool hello_valid = false;

    // ---- 업로드 진행 ----
    std::uint64_t total_bytes = 0;
    std::uint64_t bytes_sent = 0;
    std::uint64_t bytes_consumed = 0;  // 서버가 파싱까지 끝낸 양 (UPLOAD_ACK)
    double send_mibps = 0.0;

    // ---- 서버가 UPLOAD_ACK 으로 보내주는 진행 중 통계 ----
    std::uint64_t lines_parsed = 0;
    std::uint64_t accepted_lines = 0;
    std::uint64_t rejected_lines = 0;
    std::uint64_t spd_samples = 0;
    std::string spd_average;
    std::vector<std::string> module_names;
    std::vector<std::uint64_t> module_counts;

    // ---- ANALYZE_DONE ----
    bool result_valid = false;
    std::uint64_t result_total_lines = 0;
    std::uint64_t result_accepted = 0;
    std::uint64_t result_rejected = 0;
    std::uint64_t result_oversize = 0;
    std::uint64_t result_spd_samples = 0;
    std::string result_spd_average;
    std::uint64_t result_elapsed_ms = 0;
    std::uint64_t result_peak_rss_kb = 0;

    // 수신한 result.csv 원문 (Done 이후에만 채워진다).
    std::string csv;
};

class TransferClient {
public:
    TransferClient() = default;
    ~TransferClient();

    TransferClient(const TransferClient&) = delete;
    TransferClient& operator=(const TransferClient&) = delete;

    // 접속 확인: connect + HELLO/HELLO_ACK + BYE. 결과는 snapshot 으로 본다.
    void start_probe(std::string host, std::uint16_t port, std::string client_name);

    // 업로드 세션 전체: connect -> HELLO -> UPLOAD -> ANALYZE -> RESULT -> BYE.
    void start_upload(std::string host, std::uint16_t port, std::string client_name,
                      std::string file_path);

    void request_cancel() noexcept { cancel_.store(true, std::memory_order_relaxed); }

    bool busy() const noexcept { return busy_.load(std::memory_order_acquire); }

    Snapshot snapshot() const;

    // 워커가 쌓아 둔 로그 줄을 가져간다 (가져간 줄은 큐에서 사라진다).
    std::vector<std::string> drain_log();

private:
    struct Frame {
        byda::MsgType type = byda::MsgType::Bye;
        std::vector<std::uint8_t> payload;
    };

    void run_probe(std::string host, std::uint16_t port, std::string client_name);
    void run_upload(std::string host, std::uint16_t port, std::string client_name,
                    std::string file_path);

    bool send_frame(TcpSocket& sock, byda::MsgType type,
                    const std::vector<std::uint8_t>& payload, std::string& err);
    bool read_frame(TcpSocket& sock, Frame& out, int idle_timeout_ms, std::string& err);

    bool handshake(TcpSocket& sock, const std::string& client_name, std::string& err);
    void apply_upload_ack(const Frame& f);
    bool receive_result(TcpSocket& sock, std::uint64_t expected_size, std::string& err);
    void send_cancel_and_wait(TcpSocket& sock);

    // 실패로 끝낸다. ERROR 프레임이면 서버 메시지를 사유로 쓴다.
    void fail(const std::string& why);

    void set_phase(Phase p);
    void log(const std::string& line);

    bool begin_job();      // busy_ 를 원자적으로 잡는다. 실패하면 이미 실행 중.
    void finish_job();     // 스레드 join 준비 + busy_ 해제

    mutable std::mutex mu_;
    Snapshot state_;                    // mu_ 로 보호
    std::deque<std::string> log_;       // mu_ 로 보호

    std::thread worker_;
    std::atomic<bool> busy_{false};
    std::atomic<bool> cancel_{false};
};

}  // namespace bydacli
