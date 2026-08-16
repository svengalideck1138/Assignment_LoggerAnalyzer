// 리슨 소켓과 세션 수명을 관리한다.
//
// libuv 는 단일 스레드 이벤트 루프다. 모든 세션이 같은 스레드에서 돌기
// 때문에 세션 간 공유 자료에 락이 필요 없다. 500MB 업로드는 read 콜백마다
// 수백 KB 씩 잘려서 들어오고, 각 조각을 파싱하는 데 수 밀리초밖에 걸리지
// 않으므로 다른 연결이 굶지 않는다.

#pragma once

#include <uv.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "app/Config.h"
#include "net/ISessionCallback.h"
#include "net/ITcpServer.h"

namespace byda {

class TcpSession;

// ITcpServer 의 libuv 구현.
// ISessionCallback 도 구현해서, 세션이 구체 서버 타입을 몰라도
// 자신의 소멸 시점을 알릴 수 있게 한다.
class TcpServer : public ITcpServer, public ISessionCallback {
public:
    TcpServer(uv_loop_t* loop, const Config& cfg) noexcept;
    ~TcpServer() override;

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    // ITcpServer
    bool start(std::string& err) override;

    // SIGINT/SIGTERM 또는 명시적 호출로 정리 종료를 시작한다.
    void begin_shutdown(const char* reason) override;

    std::size_t session_count() const noexcept override { return sessions_.size(); }

    // ISessionCallback - 세션이 자기 핸들을 모두 닫은 뒤 호출한다.
    // 여기서 세션 객체가 소멸한다.
    void on_session_closed(std::uint32_t id) override;

    const Config& config() const noexcept { return cfg_; }
    uv_loop_t* loop() const noexcept { return loop_; }

private:
    static void on_connection(uv_stream_t* server, int status);
    static void on_signal(uv_signal_t* handle, int signum);
    static void on_handle_closed(uv_handle_t* handle);
    static void on_idle_log(uv_timer_t* t);

    void accept_one();

    uv_loop_t* loop_ = nullptr;
    Config cfg_;

    uv_tcp_t listener_{};
    uv_signal_t sig_int_{};
    uv_signal_t sig_term_{};
    uv_timer_t idle_log_timer_{};

    bool listener_inited_ = false;
    bool signals_inited_ = false;
    bool idle_log_inited_ = false;
    bool shutting_down_ = false;

    std::map<std::uint32_t, std::unique_ptr<TcpSession>> sessions_;
    std::uint32_t next_id_ = 1;
};

}  // namespace byda
