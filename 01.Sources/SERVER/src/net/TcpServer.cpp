#include "net/TcpServer.h"

#include <signal.h>
#include <spdlog/spdlog.h>

#include <vector>

#include "net/Protocol.h"
#include "net/TcpSession.h"
#include "util/Logger.h"

namespace byda {
namespace {

// 세션이 하나도 없을 때 "수신 대기 중"을 알리는 주기 (ms).
constexpr std::uint64_t kIdleLogIntervalMs = 10000;

uv_handle_t* as_handle(uv_tcp_t* t) noexcept { return reinterpret_cast<uv_handle_t*>(t); }
uv_handle_t* as_handle(uv_signal_t* t) noexcept { return reinterpret_cast<uv_handle_t*>(t); }
uv_handle_t* as_handle(uv_timer_t* t) noexcept { return reinterpret_cast<uv_handle_t*>(t); }
uv_stream_t* as_stream(uv_tcp_t* t) noexcept { return reinterpret_cast<uv_stream_t*>(t); }

}  // namespace

TcpServer::TcpServer(uv_loop_t* loop, const Config& cfg) noexcept : loop_(loop), cfg_(cfg) {}

TcpServer::~TcpServer() = default;

bool TcpServer::start(std::string& err) {
    int rc = uv_tcp_init(loop_, &listener_);
    if (rc != 0) {
        err = std::string("uv_tcp_init: ") + uv_strerror(rc);
        return false;
    }
    listener_.data = this;
    listener_inited_ = true;

    struct sockaddr_in addr;
    rc = uv_ip4_addr(cfg_.bind_addr.c_str(), static_cast<int>(cfg_.port), &addr);
    if (rc != 0) {
        err = "invalid bind address '" + cfg_.bind_addr + "': " + uv_strerror(rc);
        return false;
    }

    // UV_TCP_REUSEADDR 이 기본이라 재시작 시 TIME_WAIT 에 걸리지 않는다.
    rc = uv_tcp_bind(&listener_, reinterpret_cast<const struct sockaddr*>(&addr), 0);
    if (rc != 0) {
        err = "bind " + cfg_.bind_addr + ":" + std::to_string(cfg_.port) + ": " + uv_strerror(rc);
        return false;
    }

    rc = uv_listen(as_stream(&listener_), cfg_.backlog, &TcpServer::on_connection);
    if (rc != 0) {
        err = std::string("uv_listen: ") + uv_strerror(rc);
        return false;
    }

    // SIGINT/SIGTERM 을 이벤트 루프 안에서 받는다. 시그널 핸들러에서
    // async-signal-safe 함수만 써야 하는 제약을 libuv 가 처리해 주므로,
    // 콜백 안에서는 평범한 코드를 그대로 쓸 수 있다.
    if (uv_signal_init(loop_, &sig_int_) == 0 && uv_signal_init(loop_, &sig_term_) == 0) {
        sig_int_.data = this;
        sig_term_.data = this;
        uv_signal_start(&sig_int_, &TcpServer::on_signal, SIGINT);
        uv_signal_start(&sig_term_, &TcpServer::on_signal, SIGTERM);
        signals_inited_ = true;
    } else {
        spdlog::warn("failed to install signal handlers");
    }

    // 세션이 없는 동안 10초마다 "수신 대기 중"을 남긴다. 서버가 조용한 것이
    // 죽어서인지 한가해서인지 로그만 보고 구분할 수 있게 하기 위해서다.
    // 세션이 붙어 있는 동안에는 콜백이 아무것도 찍지 않으므로 전송 로그를
    // 방해하지 않는다.
    if (uv_timer_init(loop_, &idle_log_timer_) == 0) {
        idle_log_timer_.data = this;
        uv_timer_start(&idle_log_timer_, &TcpServer::on_idle_log, kIdleLogIntervalMs,
                       kIdleLogIntervalMs);
        idle_log_inited_ = true;
    } else {
        spdlog::warn("failed to install the idle-log timer");
    }

    spdlog::info("listening on {}:{}  max-sessions={}  read-buffer={}  idle-timeout={} ms  "
                 "upload-stall-timeout={} ms",
                 cfg_.bind_addr, cfg_.port, cfg_.max_sessions,
                 human_bytes(cfg_.read_buffer_bytes), cfg_.idle_timeout_ms,
                 cfg_.upload_stall_timeout_ms);
    return true;
}

void TcpServer::on_idle_log(uv_timer_t* t) {
    auto* self = static_cast<TcpServer*>(t->data);
    if (self == nullptr || self->shutting_down_ || !self->sessions_.empty()) {
        return;
    }
    spdlog::info("waiting for a connection on {}:{}", self->cfg_.bind_addr, self->cfg_.port);
}

void TcpServer::on_connection(uv_stream_t* server, int status) {
    auto* self = static_cast<TcpServer*>(server->data);
    if (self == nullptr) {
        return;
    }

    if (status != 0) {
        spdlog::warn("accept failed: {}", uv_strerror(status));
        return;
    }

    self->accept_one();
}

void TcpServer::accept_one() {
    if (shutting_down_) {
        return;
    }

    const std::uint32_t id = next_id_++;

    auto session = std::make_unique<TcpSession>(*this, cfg_, id);
    if (!session->init(loop_, cfg_.read_buffer_bytes, cfg_.idle_timeout_ms,
                       cfg_.upload_stall_timeout_ms)) {
        return;
    }

    const int rc = uv_accept(as_stream(&listener_), as_stream(session->tcp()));
    if (rc != 0) {
        spdlog::warn("uv_accept: {}", uv_strerror(rc));
        session->close("accept failed");
        // close() 가 remove_session 을 호출할 수 있지만 아직 맵에 없으므로
        // 아무 일도 일어나지 않는다. unique_ptr 은 여기서 소멸한다... 가 아니라,
        // 핸들이 닫히는 콜백에서 remove_session 이 불린다. 맵에 넣어 두어야
        // 그때 정상적으로 회수된다.
        sessions_.emplace(id, std::move(session));
        return;
    }

    TcpSession* raw = session.get();
    sessions_.emplace(id, std::move(session));

    // 상한을 넘으면 연결은 받되 이유를 알려주고 즉시 닫는다.
    // 백로그에 방치하면 클라이언트는 연결된 줄 알고 계속 기다린다.
    if (static_cast<int>(sessions_.size()) > cfg_.max_sessions) {
        raw->reject(ErrorCode::ServerBusy, "server busy: too many concurrent sessions");
        return;
    }

    raw->start();
}

void TcpServer::on_signal(uv_signal_t* handle, int signum) {
    auto* self = static_cast<TcpServer*>(handle->data);
    if (self == nullptr) {
        return;
    }
    self->begin_shutdown(signum == SIGINT ? "SIGINT" : "SIGTERM");
}

void TcpServer::begin_shutdown(const char* reason) {
    if (shutting_down_) {
        return;
    }
    shutting_down_ = true;

    spdlog::info("shutdown requested ({}), {} active session(s)", reason, sessions_.size());

    if (listener_inited_ && uv_is_closing(as_handle(&listener_)) == 0) {
        uv_close(as_handle(&listener_), nullptr);
    }
    // 반복 타이머를 닫지 않으면 루프에 활성 핸들이 남아 uv_run 이
    // 영원히 반환하지 않는다.
    if (idle_log_inited_) {
        uv_timer_stop(&idle_log_timer_);
        if (uv_is_closing(as_handle(&idle_log_timer_)) == 0) {
            uv_close(as_handle(&idle_log_timer_), nullptr);
        }
    }
    if (signals_inited_) {
        uv_signal_stop(&sig_int_);
        uv_signal_stop(&sig_term_);
        if (uv_is_closing(as_handle(&sig_int_)) == 0) {
            uv_close(as_handle(&sig_int_), nullptr);
        }
        if (uv_is_closing(as_handle(&sig_term_)) == 0) {
            uv_close(as_handle(&sig_term_), nullptr);
        }
    }

    // close() 안에서 remove_session 이 불려 맵이 바뀔 수 있으므로
    // id 목록을 먼저 복사해 두고 순회한다.
    std::vector<std::uint32_t> ids;
    ids.reserve(sessions_.size());
    for (const auto& kv : sessions_) {
        ids.push_back(kv.first);
    }
    for (const std::uint32_t id : ids) {
        auto it = sessions_.find(id);
        if (it != sessions_.end() && it->second) {
            it->second->close("server shutting down");
        }
    }
}

void TcpServer::on_session_closed(std::uint32_t id) {
    auto it = sessions_.find(id);
    if (it == sessions_.end()) {
        return;
    }
    sessions_.erase(it);  // unique_ptr 소멸 -> 세션 객체 파괴

    if (cfg_.verbose) {
        spdlog::debug("session {} removed, {} remaining", id, sessions_.size());
    }
}

void TcpServer::on_handle_closed(uv_handle_t* /*handle*/) {}

}  // namespace byda
