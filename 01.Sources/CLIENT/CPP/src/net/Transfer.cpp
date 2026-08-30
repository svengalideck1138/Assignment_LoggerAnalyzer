#include "Transfer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>

#include "Payload.h"

namespace bydacli {
namespace {

// 클라이언트가 HELLO 에 싣는 프로토콜 버전 (C# 클라이언트와 동일).
constexpr std::uint16_t kClientProtocolVersion = 1;

// 청크 하나를 쓰는 데 이보다 오래 걸리면 회선이 끊긴 것으로 본다.
constexpr int kWriteStallTimeoutMs = 20000;

// 프레임을 기다리는 유휴 한도. 서버는 수신과 동시에 파싱하므로
// UPLOAD_END 직후의 ANALYZE_DONE 도 이 한도 안에 온다.
constexpr int kIdleTimeoutMs = 120000;

// 취소 후 서버의 ERROR(Cancelled) 응답을 기다리는 한도.
constexpr int kCancelAckTimeoutMs = 5000;

// 조작된 크기로 거대한 버퍼를 잡지 않도록 result.csv 에 상한을 둔다.
constexpr std::uint64_t kMaxCsvBytes = 64ull * 1024 * 1024;

// 접속 제한 시간.
constexpr int kConnectTimeoutMs = 5000;

// 유휴 루프에서 명령을 기다리는 슬라이스. 이 주기로 소켓 생존도 살핀다.
constexpr int kIdleSliceMs = 200;

// CANCEL/BYE 처럼 취소 중에도 보내야 하는 프레임이 쓰는 '취소 없음' 플래그.
const std::atomic<bool> g_never_cancel{false};

std::int64_t now_ms() noexcept {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// 로컬 시각 "[HH:MM:SS.mmm] " 접두어.
std::string local_time_prefix() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t tt = system_clock::to_time_t(now);
    const int ms = static_cast<int>(
        duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);

    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &tt);
#else
    localtime_r(&tt, &local);
#endif
    char buf[24];
    std::snprintf(buf, sizeof(buf), "[%02d:%02d:%02d.%03d] ", local.tm_hour, local.tm_min,
                  local.tm_sec, ms);
    return buf;
}

}  // namespace

std::string human_bytes(std::uint64_t n) {
    constexpr double ki = 1024.0;
    const double v = static_cast<double>(n);
    char buf[32];
    if (v >= ki * ki * ki) {
        std::snprintf(buf, sizeof(buf), "%.2f GiB", v / (ki * ki * ki));
    } else if (v >= ki * ki) {
        std::snprintf(buf, sizeof(buf), "%.1f MiB", v / (ki * ki));
    } else if (v >= ki) {
        std::snprintf(buf, sizeof(buf), "%.1f KiB", v / ki);
    } else {
        std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(n));
    }
    return buf;
}

const char* phase_text(Phase p) noexcept {
    switch (p) {
        case Phase::Idle: return "idle";
        case Phase::Connecting: return "connecting";
        case Phase::Handshaking: return "handshaking";
        case Phase::Connected: return "connected";
        case Phase::Uploading: return "uploading";
        case Phase::WaitingAnalysis: return "waiting for analysis";
        case Phase::ReceivingResult: return "receiving result";
        case Phase::Done: return "done";
        case Phase::Cancelled: return "cancelled";
        case Phase::Failed: return "failed";
    }
    return "?";
}

TransferClient::~TransferClient() {
    request_cancel();
    disconnect();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void TransferClient::connect(std::string host, std::uint16_t port,
                             std::string client_name) {
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;  // 세션이 이미 살아 있다
    }
    if (worker_.joinable()) {
        worker_.join();  // 끝난 이전 스레드를 회수한다
    }
    cancel_.store(false, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(cmd_mu_);
        cmds_.clear();
    }
    {
        std::lock_guard<std::mutex> lk(mu_);
        state_ = Snapshot{};
        csv_.clear();
    }
    worker_ = std::thread(&TransferClient::run_session, this, std::move(host), port,
                          std::move(client_name));
}

void TransferClient::disconnect() {
    std::lock_guard<std::mutex> lk(cmd_mu_);
    // 아직 시작하지 않은 업로드보다 해제가 우선한다. 큐에 남겨 두면
    // 500MB 업로드를 다 마친 뒤에야 끊게 된다.
    cmds_.clear();
    cmds_.push_back(Command{Cmd::Disconnect, {}});
    cmd_cv_.notify_all();
}

void TransferClient::start_upload(std::string file_path) {
    // 새 업로드를 시작하므로 이전 업로드를 향했던 취소 플래그를 여기서
    // 내린다. 워커가 명령을 집어드는 시점에 내리면, 큐에 넣은 '뒤'에 누른
    // Cancel 까지 함께 지워져 사용자의 취소가 유실된다. 여기서 내리면
    // 그 Cancel 은 남아서 업로드가 시작하자마자 취소된다.
    cancel_.store(false, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lk(cmd_mu_);
    cmds_.push_back(Command{Cmd::Upload, std::move(file_path)});
    cmd_cv_.notify_all();
}

Snapshot TransferClient::snapshot() const {
    std::lock_guard<std::mutex> lk(mu_);
    return state_;
}

std::string TransferClient::csv_copy() const {
    std::lock_guard<std::mutex> lk(mu_);
    return csv_;
}

std::vector<std::string> TransferClient::drain_log() {
    std::vector<std::string> out;
    std::lock_guard<std::mutex> lk(mu_);
    out.assign(log_.begin(), log_.end());
    log_.clear();
    return out;
}

void TransferClient::set_phase(Phase p) {
    std::lock_guard<std::mutex> lk(mu_);
    state_.phase = p;
}

void TransferClient::set_connected(bool on) {
    std::lock_guard<std::mutex> lk(mu_);
    state_.connected = on;
}

void TransferClient::log(const std::string& line) {
    std::lock_guard<std::mutex> lk(mu_);
    log_.push_back(local_time_prefix() + line);
    // UI 가 오래 안 가져가도 무한히 자라지 않게 한다.
    while (log_.size() > 2000) {
        log_.pop_front();
    }
}

void TransferClient::fail(const std::string& why) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        state_.phase = Phase::Failed;
        state_.connected = false;
        state_.error = why;
    }
    log("[ERROR] " + why);
}

// ------------------------------------------------------------------ frames

bool TransferClient::send_frame(TcpSocket& sock, byda::MsgType type,
                                const std::vector<std::uint8_t>& payload, std::string& err) {
    byda::FrameHeader h;
    h.type = type;
    h.payload_len = payload.size();
    byda::HeaderBytes head{};
    byda::encode_header(h, head);

    if (!sock.send_all(head.data(), head.size(), kWriteStallTimeoutMs, cancel_, err)) {
        return false;
    }
    if (!payload.empty() &&
        !sock.send_all(payload.data(), payload.size(), kWriteStallTimeoutMs, cancel_, err)) {
        return false;
    }
    return true;
}

bool TransferClient::read_frame(TcpSocket& sock, Frame& out, int idle_timeout_ms,
                                std::string& err) {
    byda::HeaderBytes head{};
    if (!sock.recv_exactly(head.data(), head.size(), idle_timeout_ms, cancel_, err)) {
        return false;
    }

    byda::FrameHeader h;
    byda::decode_header(head, h);

    if (h.magic != byda::kMagic) {
        err = "bad magic from server";
        return false;
    }
    if (h.version != byda::kVersion) {
        err = "unsupported protocol version from server: " + std::to_string(h.version);
        return false;
    }
    if (h.payload_len > byda::kMaxPayload) {
        err = "payload too large from server: " + std::to_string(h.payload_len) + " bytes";
        return false;
    }

    out.type = h.type;
    out.payload.resize(static_cast<std::size_t>(h.payload_len));
    if (h.payload_len > 0 &&
        !sock.recv_exactly(out.payload.data(), out.payload.size(), idle_timeout_ms, cancel_,
                           err)) {
        return false;
    }
    return true;
}

namespace {

// ERROR 프레임을 사람이 읽을 문자열로.
std::string server_error_text(const std::vector<std::uint8_t>& payload) {
    PayloadReader r(payload);
    std::uint16_t code = 0;
    std::string msg;
    if (!r.u16(code) || !r.str16(msg)) {
        return "malformed ERROR payload";
    }
    return "server error " + std::to_string(code) + ": " + msg;
}

}  // namespace

// --------------------------------------------------------------- handshake

bool TransferClient::handshake(TcpSocket& sock, const std::string& client_name,
                               std::string& err) {
    set_phase(Phase::Handshaking);

    PayloadWriter w;
    w.u16(kClientProtocolVersion);
    w.str16(client_name);
    if (!send_frame(sock, byda::MsgType::Hello, w.bytes(), err)) {
        return false;
    }
    log("-> HELLO             " + client_name);

    Frame f;
    if (!read_frame(sock, f, kIdleTimeoutMs, err)) {
        return false;
    }
    if (f.type == byda::MsgType::Error) {
        err = server_error_text(f.payload);
        return false;
    }
    if (f.type != byda::MsgType::HelloAck) {
        err = std::string("expected HELLO_ACK but received ") + byda::msg_name(f.type);
        return false;
    }

    PayloadReader r(f.payload);
    HelloInfo hello;
    if (!r.u32(hello.session_id) || !r.u32(hello.max_chunk) ||
        !r.str16(hello.observed_peer) || !r.str16(hello.server_version)) {
        err = "malformed HELLO_ACK payload";
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(mu_);
        state_.hello = hello;
        state_.hello_valid = true;
    }
    log("<- HELLO_ACK         session=" + std::to_string(hello.session_id) +
        "  peer=" + hello.observed_peer + "  server=" + hello.server_version);
    return true;
}

// ------------------------------------------------------------ session loop

void TransferClient::run_session(std::string host, std::uint16_t port,
                                 std::string client_name) {
    set_phase(Phase::Connecting);
    log("connecting to " + host + ":" + std::to_string(port) + " ...");

    TcpSocket sock;
    std::string err;
    if (!sock.connect(host, port, kConnectTimeoutMs, cancel_, err)) {
        fail(err);
        busy_.store(false, std::memory_order_release);
        return;
    }
    if (!handshake(sock, client_name, err)) {
        fail(err);
        busy_.store(false, std::memory_order_release);
        return;
    }

    set_connected(true);
    set_phase(Phase::Connected);

    // ---- 명령 루프: 업로드 / 해제 요청과 소켓 생존을 함께 살핀다 ----
    bool alive = true;
    while (alive) {
        bool has_cmd = false;
        Command cmd;
        {
            std::unique_lock<std::mutex> lk(cmd_mu_);
            cmd_cv_.wait_for(lk, std::chrono::milliseconds(kIdleSliceMs),
                             [this] { return !cmds_.empty(); });
            if (!cmds_.empty()) {
                cmd = std::move(cmds_.front());
                cmds_.pop_front();
                has_cmd = true;
            }
        }

        if (has_cmd && cmd.kind == Cmd::Disconnect) {
            (void)send_frame(sock, byda::MsgType::Bye, {}, err);
            log("-> BYE               disconnected");
            break;
        }

        if (has_cmd && cmd.kind == Cmd::Upload) {
            alive = do_upload(sock, cmd.file);
            continue;
        }

        // 유휴: 서버가 보낸 것이 있으면 소화한다 (종료 통보, EOF 등).
        while (alive && sock.readable_now()) {
            Frame f;
            if (!read_frame(sock, f, 5000, err)) {
                fail("connection lost: " + err);
                alive = false;
                break;
            }
            if (f.type == byda::MsgType::Error) {
                fail(server_error_text(f.payload));
                alive = false;
            } else {
                log(std::string("<- ") + byda::msg_name(f.type) + " (ignored while idle)");
            }
        }
    }

    sock.close();
    set_connected(false);
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (state_.phase != Phase::Failed) {
            state_.phase = Phase::Idle;
        }
    }
    busy_.store(false, std::memory_order_release);
}

// ------------------------------------------------------------------ upload

void TransferClient::apply_upload_ack(const Frame& f) {
    PayloadReader r(f.payload);

    std::uint64_t consumed = 0, lines = 0;
    if (!r.u64(consumed) || !r.u64(lines)) {
        return;
    }

    std::lock_guard<std::mutex> lk(mu_);
    state_.bytes_consumed = consumed;
    state_.lines_parsed = lines;

    // 아래 필드는 서버 버전에 따라 없을 수도 있다. 읽히는 만큼만 반영한다.
    std::uint64_t v = 0;
    if (r.u64(v)) state_.accepted_lines = v;
    if (r.u64(v)) state_.rejected_lines = v;
    if (r.u64(v)) state_.spd_samples = v;
    std::string avg;
    if (r.str16(avg)) state_.spd_average = avg;

    std::uint8_t module_count = 0;
    if (r.u8(module_count) && module_count > 0 && module_count <= 64) {
        if (state_.module_counts.size() != module_count) {
            state_.module_counts.assign(module_count, 0);
        }
        for (std::uint8_t i = 0; i < module_count; ++i) {
            std::uint64_t c = 0;
            if (!r.u64(c)) break;
            state_.module_counts[i] = c;
        }
    }
}

void TransferClient::send_cancel_and_wait(TcpSocket& sock) {
    std::string err;
    if (!send_frame(sock, byda::MsgType::Cancel, {}, err)) {
        return;  // 이미 끊긴 연결이라면 알릴 방법이 없다. 정상 경로다.
    }
    log("-> CANCEL            waiting for the server to acknowledge");

    // 서버가 소켓 버퍼에 남은 청크를 소화한 뒤 ERROR(Cancelled) 를 보낸다.
    const std::int64_t deadline = now_ms() + kCancelAckTimeoutMs;
    while (now_ms() < deadline) {
        Frame f;
        byda::HeaderBytes head{};
        std::string e2;
        const int left = static_cast<int>(deadline - now_ms());
        if (!sock.recv_exactly(head.data(), head.size(), left > 0 ? left : 1,
                               g_never_cancel, e2)) {
            return;
        }
        byda::FrameHeader h;
        byda::decode_header(head, h);
        if (h.magic != byda::kMagic || h.payload_len > byda::kMaxPayload) {
            return;
        }
        f.type = h.type;
        f.payload.resize(static_cast<std::size_t>(h.payload_len));
        if (h.payload_len > 0 &&
            !sock.recv_exactly(f.payload.data(), f.payload.size(), kCancelAckTimeoutMs,
                               g_never_cancel, e2)) {
            return;
        }

        if (f.type == byda::MsgType::Error) {
            log("<- " + server_error_text(f.payload));
            return;
        }
        // 취소 직전까지 밀려 있던 진행 보고 등은 버린다.
    }
}

bool TransferClient::do_upload(TcpSocket& sock, const std::string& file_path) {
    namespace fs = std::filesystem;

    // 새 업로드: 이전 결과/진행 표시를 지우되 연결 정보는 유지한다.
    {
        std::lock_guard<std::mutex> lk(mu_);
        const HelloInfo hello = state_.hello;
        const bool connected = state_.connected;
        Snapshot fresh;
        fresh.hello = hello;
        fresh.hello_valid = true;
        fresh.connected = connected;
        state_ = std::move(fresh);
        csv_.clear();
    }

    // ---- 0) 파일 확인 ----
    std::error_code ec;
    const std::uint64_t total = fs::file_size(fs::u8path(file_path), ec);
    if (ec) {
        set_phase(Phase::Connected);
        log("[ERROR] cannot read file: " + file_path);
        return true;  // 연결은 멀쩡하다
    }
    const std::string file_name = fs::u8path(file_path).filename().u8string();

    std::string err;

    // ---- 1) UPLOAD_BEGIN ----
    {
        PayloadWriter w;
        w.u64(total);
        w.str16(file_name);
        if (!send_frame(sock, byda::MsgType::UploadBegin, w.bytes(), err)) {
            fail(err);
            return false;
        }
    }
    log("-> UPLOAD_BEGIN      " + file_name + "  " + human_bytes(total));

    Frame ack;
    if (!read_frame(sock, ack, kIdleTimeoutMs, err)) {
        fail(err);
        return false;
    }
    if (ack.type == byda::MsgType::Error) {
        fail(server_error_text(ack.payload));
        return false;
    }
    if (ack.type != byda::MsgType::UploadBeginAck) {
        fail(std::string("expected UPLOAD_BEGIN_ACK but received ") +
             byda::msg_name(ack.type));
        return false;
    }

    std::uint32_t upload_id = 0, chunk_size = 0;
    std::vector<std::string> module_names;
    {
        PayloadReader r(ack.payload);
        if (!r.u32(upload_id) || !r.u32(chunk_size)) {
            fail("malformed UPLOAD_BEGIN_ACK payload");
            return false;
        }
        if (chunk_size == 0 || chunk_size > 4u * 1024 * 1024) {
            chunk_size = 1024 * 1024;
        }
        // 모듈 이름은 여기서 한 번만 온다. 이후 UPLOAD_ACK 에는 카운트만 온다.
        std::uint8_t count = 0;
        if (r.u8(count) && count > 0 && count <= 64) {
            for (std::uint8_t i = 0; i < count; ++i) {
                std::string name;
                if (!r.str16(name)) {
                    module_names.clear();
                    break;
                }
                module_names.push_back(std::move(name));
            }
        }
    }
    log("<- UPLOAD_BEGIN_ACK  upload_id=" + std::to_string(upload_id) +
        "  chunk=" + human_bytes(chunk_size) +
        "  modules=" + std::to_string(module_names.size()));

    {
        std::lock_guard<std::mutex> lk(mu_);
        state_.total_bytes = total;
        state_.module_names = module_names;
        state_.module_counts.assign(module_names.size(), 0);
    }

    // ---- 2) UPLOAD_CHUNK 반복 ----
    set_phase(Phase::Uploading);

    std::ifstream file(fs::u8path(file_path), std::ios::binary);
    if (!file) {
        fail("cannot open file: " + file_path);
        return false;
    }

    // 헤더 16바이트 + 데이터를 한 버퍼에 담아 send 한 번으로 보낸다.
    // 전송 내내 재사용하는 유일한 버퍼다.
    std::vector<std::uint8_t> buf(byda::kHeaderSize + chunk_size);
    std::uint64_t sent = 0;
    const std::int64_t t0 = now_ms();

    // 로그는 UI 갱신보다 훨씬 성기게 남긴다.
    constexpr std::uint64_t kLogIntervalBytes = 32ull * 1024 * 1024;
    std::uint64_t last_log_bytes = 0;

    for (;;) {
        // 취소는 어떤 I/O 보다 먼저, 이 지점에서만 판단한다.
        if (cancel_.load(std::memory_order_relaxed)) {
            send_cancel_and_wait(sock);
            set_phase(Phase::Cancelled);
            log("upload cancelled by user");
            return true;  // 서버는 세션을 Ready 로 되돌린다. 연결 유지.
        }

        // 선언한 총량까지만 읽는다. 업로드 도중 파일이 자라도 서버가
        // "선언보다 많이 왔다"(SizeMismatch) 로 연결을 끊는 일이 없다.
        const std::uint64_t remaining = total - sent;
        if (remaining == 0) {
            break;
        }
        const std::size_t want =
            static_cast<std::size_t>(std::min<std::uint64_t>(chunk_size, remaining));

        file.read(reinterpret_cast<char*>(buf.data() + byda::kHeaderSize),
                  static_cast<std::streamsize>(want));
        const std::streamsize n = file.gcount();
        if (n <= 0) {
            break;
        }

        byda::FrameHeader h;
        h.type = byda::MsgType::UploadChunk;
        h.payload_len = static_cast<std::uint64_t>(n);
        byda::HeaderBytes head{};
        byda::encode_header(h, head);
        std::copy(head.begin(), head.end(), buf.begin());

        if (!sock.send_all(buf.data(), byda::kHeaderSize + static_cast<std::size_t>(n),
                           kWriteStallTimeoutMs, cancel_, err)) {
            if (cancel_.load(std::memory_order_relaxed)) {
                send_cancel_and_wait(sock);
                set_phase(Phase::Cancelled);
                log("upload cancelled by user");
                return true;
            }
            fail(err);
            return false;
        }
        sent += static_cast<std::uint64_t>(n);

        // 서버가 보낸 UPLOAD_ACK 을 논블로킹으로 걷어낸다.
        // 읽지 않고 두면 서버의 송신 버퍼가 차서 교착으로 갈 수 있다.
        while (sock.readable_now()) {
            Frame f;
            if (!read_frame(sock, f, 5000, err)) {
                fail(err);
                return false;
            }
            if (f.type == byda::MsgType::UploadAck) {
                apply_upload_ack(f);
            } else if (f.type == byda::MsgType::Error) {
                fail(server_error_text(f.payload));
                return false;
            }
            // 그 밖의 메시지는 업로드 중에 올 이유가 없으므로 무시한다.
        }

        // 진행 상황 갱신 (UI 는 매 프레임 스냅샷을 복사해 간다).
        {
            const double secs = static_cast<double>(now_ms() - t0) / 1000.0;
            std::lock_guard<std::mutex> lk(mu_);
            state_.bytes_sent = sent;
            state_.send_mibps =
                secs > 0.0 ? (static_cast<double>(sent) / (1024.0 * 1024.0)) / secs : 0.0;
        }

        if (sent - last_log_bytes >= kLogIntervalBytes) {
            char line[160];
            const double secs = static_cast<double>(now_ms() - t0) / 1000.0;
            const double mibps =
                secs > 0.0 ? (static_cast<double>(sent) / (1024.0 * 1024.0)) / secs : 0.0;
            std::snprintf(line, sizeof(line), "-> UPLOAD_CHUNK      %s / %s  (%.1f%%)  %.1f MiB/s",
                          human_bytes(sent).c_str(), human_bytes(total).c_str(),
                          total > 0 ? 100.0 * static_cast<double>(sent) /
                                          static_cast<double>(total)
                                    : 0.0,
                          mibps);
            log(line);
            last_log_bytes = sent;
        }
    }

    // ---- 2.5) 로컬 파일 이상 검사 ----
    // 선언한 총량을 채우지 못하고 루프가 끝났다면 서버가 아니라 이쪽
    // 문제다: 디스크 읽기 오류(badbit), 또는 업로드 도중 파일이 줄어든
    // 경우다. 이대로 UPLOAD_END 를 보내면 서버가 SizeMismatch 로 연결을
    // 끊어 "server error 5" 로 잘못 보고되므로, 우리가 먼저 CANCEL 로
    // 세션을 원위치시키고 로컬 오류로 정확히 알린다. 연결은 유지된다.
    if (file.bad() || sent != total) {
        const std::string why =
            file.bad() ? "local file read error after " + human_bytes(sent)
                       : "file changed during upload: declared " + human_bytes(total) +
                             " but could only read " + human_bytes(sent);
        send_cancel_and_wait(sock);
        set_phase(Phase::Connected);
        log("[ERROR] " + why + " - upload aborted, connection stays open");
        return true;
    }

    // ---- 3) UPLOAD_END ----
    {
        PayloadWriter w;
        w.u64(sent);
        if (!send_frame(sock, byda::MsgType::UploadEnd, w.bytes(), err)) {
            fail(err);
            return false;
        }
    }
    log("-> UPLOAD_END        " + human_bytes(sent) + " sent, waiting for the analysis");
    set_phase(Phase::WaitingAnalysis);

    // ---- 4) ANALYZE_DONE 까지 남은 프레임을 소화한다 ----
    std::uint64_t csv_size = 0;
    for (;;) {
        if (cancel_.load(std::memory_order_relaxed)) {
            send_cancel_and_wait(sock);
            set_phase(Phase::Cancelled);
            log("cancelled while waiting for the analysis");
            return true;
        }

        Frame f;
        if (!read_frame(sock, f, kIdleTimeoutMs, err)) {
            fail(err);
            return false;
        }

        if (f.type == byda::MsgType::UploadAck) {
            apply_upload_ack(f);
            continue;
        }
        if (f.type == byda::MsgType::Error) {
            fail(server_error_text(f.payload));
            return false;
        }
        if (f.type == byda::MsgType::AnalyzeDone) {
            PayloadReader r(f.payload);
            std::uint64_t bytes = 0, lines = 0, accepted = 0, rejected = 0, oversize = 0;
            std::uint64_t spd_n = 0, elapsed = 0, rss = 0;
            std::string avg;
            if (!r.u64(bytes) || !r.u64(lines) || !r.u64(accepted) || !r.u64(rejected) ||
                !r.u64(oversize) || !r.u64(spd_n) || !r.str16(avg) || !r.u64(elapsed) ||
                !r.u64(rss) || !r.u64(csv_size)) {
                fail("malformed ANALYZE_DONE payload");
                return false;
            }

            {
                std::lock_guard<std::mutex> lk(mu_);
                state_.bytes_consumed = bytes;
                state_.result_valid = true;
                state_.result_total_lines = lines;
                state_.result_accepted = accepted;
                state_.result_rejected = rejected;
                state_.result_oversize = oversize;
                state_.result_spd_samples = spd_n;
                state_.result_spd_average = avg;
                state_.result_elapsed_ms = elapsed;
                state_.result_peak_rss_kb = rss;
                state_.lines_parsed = lines;
                state_.accepted_lines = accepted;
                state_.rejected_lines = rejected;
                state_.spd_samples = spd_n;
                state_.spd_average = avg;
            }

            char line[200];
            std::snprintf(line, sizeof(line),
                          "<- ANALYZE_DONE      lines=%llu  accepted=%llu  rejected=%llu  "
                          "spd_avg=%s  parse=%llu ms  server peak RSS=%.1f MiB",
                          static_cast<unsigned long long>(lines),
                          static_cast<unsigned long long>(accepted),
                          static_cast<unsigned long long>(rejected), avg.c_str(),
                          static_cast<unsigned long long>(elapsed),
                          static_cast<double>(rss) / 1024.0);
            log(line);
            break;
        }

        fail(std::string("unexpected message while waiting for ANALYZE_DONE: ") +
             byda::msg_name(f.type));
        return false;
    }

    // ---- 5) result.csv 수신 ----
    set_phase(Phase::ReceivingResult);
    if (!receive_result(sock, csv_size, err)) {
        fail(err);
        return false;
    }

    // 서버는 세션을 Ready 로 되돌린다. 연결을 유지한 채 완료로 표시한다.
    set_phase(Phase::Done);
    log("upload finished, connection stays open");
    return true;
}

bool TransferClient::receive_result(TcpSocket& sock, std::uint64_t expected_size,
                                    std::string& err) {
    if (expected_size > kMaxCsvBytes) {
        err = "result.csv size is out of range: " + std::to_string(expected_size);
        return false;
    }

    Frame begin;
    if (!read_frame(sock, begin, kIdleTimeoutMs, err)) {
        return false;
    }
    if (begin.type == byda::MsgType::Error) {
        err = server_error_text(begin.payload);
        return false;
    }
    if (begin.type != byda::MsgType::ResultBegin) {
        err = std::string("expected RESULT_BEGIN but received ") + byda::msg_name(begin.type);
        return false;
    }

    std::uint64_t declared = 0;
    {
        PayloadReader r(begin.payload);
        if (!r.u64(declared)) {
            err = "malformed RESULT_BEGIN payload";
            return false;
        }
    }
    if (declared > kMaxCsvBytes) {
        err = "result.csv too large: " + std::to_string(declared);
        return false;
    }
    log("<- RESULT_BEGIN      result.csv  " + human_bytes(declared));

    std::string csv;
    csv.reserve(static_cast<std::size_t>(declared));

    for (;;) {
        Frame f;
        if (!read_frame(sock, f, kIdleTimeoutMs, err)) {
            return false;
        }
        if (f.type == byda::MsgType::ResultChunk) {
            csv.append(reinterpret_cast<const char*>(f.payload.data()), f.payload.size());
            // declared 는 위에서 kMaxCsvBytes 이하로 검증됐다. 쌓는 도중에도
            // 선언한 크기를 넘어서는 순간 바로 끊어, 조작된 스트림이 상한까지
            // 메모리를 채우는 일을 막는다.
            if (csv.size() > declared) {
                err = "result.csv exceeded the declared size";
                return false;
            }
            continue;
        }
        if (f.type == byda::MsgType::ResultEnd) {
            break;
        }
        if (f.type == byda::MsgType::Error) {
            err = server_error_text(f.payload);
            return false;
        }
        err = std::string("unexpected message while receiving result.csv: ") +
              byda::msg_name(f.type);
        return false;
    }

    if (csv.size() != declared) {
        err = "result.csv size mismatch: declared " + std::to_string(declared) +
              " but received " + std::to_string(csv.size());
        return false;
    }
    log("<- RESULT_END        received " + std::to_string(csv.size()) + " bytes");

    std::lock_guard<std::mutex> lk(mu_);
    state_.csv_size = csv.size();
    csv_ = std::move(csv);
    return true;
}

}  // namespace bydacli
