// Zhenyu_LoggerAnalyzer C++ client - 전송 엔진 (워커 스레드, 상주 연결).
//
// 과제 요구사항 C2: 파일 전송은 반드시 워커 스레드로 처리하고,
// 500MB 전송 중에도 UI 스레드가 멈추면 안 된다.
//
// 연결 모델:
//   - Connect 하면 워커 스레드가 접속 + HELLO 핸드셰이크 후 '연결을 유지'
//     하며 명령을 기다린다. 서버는 업로드가 끝나면 세션을 Ready 로
//     되돌리므로 (TcpSession) 한 연결에서 여러 번 업로드할 수 있다.
//   - Disconnect 는 BYE 를 보내고 소켓을 닫은 뒤 스레드를 끝낸다.
//   - 유휴 중에도 소켓을 감시해서, 서버가 내려가거나(ERROR ShuttingDown)
//     유휴 타임아웃으로 끊으면 즉시 '연결 끊김'으로 반영한다.
//
// 스레드 규칙:
//   - UI 는 매 프레임 snapshot() 으로 복사본을 읽는다. 찢어진 값 없음.
//   - 명령(업로드/해제)은 뮤텍스 + 조건변수 큐 하나로 전달한다.
//   - 취소는 원자 플래그. 워커는 청크 사이와 poll 슬라이스마다 확인한다.
//
// 와이어 규약은 서버의 net/Protocol.h 를 그대로 include 한다.

#pragma once

#include <net/Protocol.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Socket.h"

namespace bydacli {

enum class Phase {
    Idle,             // 연결 없음
    Connecting,
    Handshaking,
    Connected,        // 연결 유지, 명령 대기
    Uploading,
    WaitingAnalysis,
    ReceivingResult,
    Done,             // 연결 유지 중, 마지막 업로드 성공
    Cancelled,        // 연결 유지 중, 마지막 업로드 취소됨
    Failed,           // 연결 없음, error 에 사유
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
    bool connected = false;
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

    // 수신한 result.csv 의 크기 (업로드 성공 이후에만 0 이 아니다).
    // 본문은 매 프레임 복사되는 스냅샷에 싣지 않고 TransferClient 가
    // 따로 보관한다. 저장할 때만 csv_copy() 로 가져간다.
    std::uint64_t csv_size = 0;
};

class TransferClient {
public:
    TransferClient() = default;
    ~TransferClient();

    TransferClient(const TransferClient&) = delete;
    TransferClient& operator=(const TransferClient&) = delete;

    // 접속하고 연결을 유지한다. 이미 세션이 살아 있으면 무시된다.
    void connect(std::string host, std::uint16_t port, std::string client_name);

    // BYE 를 보내고 연결을 끊는다.
    void disconnect();

    // 연결된 상태에서 업로드를 시작한다. 연결이 없으면 무시된다.
    void start_upload(std::string file_path);

    void request_cancel() noexcept { cancel_.store(true, std::memory_order_relaxed); }

    // 세션 워커가 살아 있는가 (연결 시도 중 포함).
    bool busy() const noexcept { return busy_.load(std::memory_order_acquire); }

    Snapshot snapshot() const;

    // 마지막 업로드가 남긴 result.csv 본문의 복사본. 없으면 빈 문자열.
    std::string csv_copy() const;

    // 워커가 쌓아 둔 로그 줄을 가져간다 (가져간 줄은 큐에서 사라진다).
    std::vector<std::string> drain_log();

private:
    struct Frame {
        byda::MsgType type = byda::MsgType::Bye;
        std::vector<std::uint8_t> payload;
    };

    enum class Cmd { Upload, Disconnect };

    struct Command {
        Cmd kind = Cmd::Disconnect;
        std::string file;  // Upload 일 때만 사용
    };

    void run_session(std::string host, std::uint16_t port, std::string client_name);

    // false 를 돌려주면 연결이 죽었다는 뜻이다 (세션 루프 종료).
    bool do_upload(TcpSocket& sock, const std::string& file_path);

    bool send_frame(TcpSocket& sock, byda::MsgType type,
                    const std::vector<std::uint8_t>& payload, std::string& err);
    bool read_frame(TcpSocket& sock, Frame& out, int idle_timeout_ms, std::string& err);

    bool handshake(TcpSocket& sock, const std::string& client_name, std::string& err);
    void apply_upload_ack(const Frame& f);
    bool receive_result(TcpSocket& sock, std::uint64_t expected_size, std::string& err);
    void send_cancel_and_wait(TcpSocket& sock);

    void fail(const std::string& why);
    void set_phase(Phase p);
    void set_connected(bool on);
    void log(const std::string& line);

    mutable std::mutex mu_;
    Snapshot state_;                    // mu_ 로 보호
    std::string csv_;                   // mu_ 로 보호. 마지막 result.csv 본문
    std::deque<std::string> log_;       // mu_ 로 보호

    // ---- 명령 큐 (UI -> 워커) ----
    // 단일 슬롯이 아니라 큐다. 워커가 깨기 전(최대 200ms)에 명령이 연달아
    // 들어와도 서로 덮어쓰지 않는다. Disconnect 는 대기 중인 업로드보다
    // 우선한다 (disconnect() 가 큐를 비우고 들어간다).
    std::mutex cmd_mu_;
    std::condition_variable cmd_cv_;
    std::deque<Command> cmds_;

    std::thread worker_;
    std::atomic<bool> busy_{false};
    std::atomic<bool> cancel_{false};
};

}  // namespace bydacli
