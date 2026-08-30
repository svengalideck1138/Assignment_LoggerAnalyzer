// Zhenyu_LoggerAnalyzer C++ client - 크로스 플랫폼 TCP 소켓 (RAII).
//
// Windows(Winsock2)와 Linux(BSD 소켓)를 하나의 인터페이스로 감싼다.
// 소켓은 논블로킹으로 두고 모든 대기를 poll 로 한다. 이유:
//   - 취소: 워커 스레드가 250ms 단위로 취소 플래그를 볼 수 있다
//   - 스톨 감지: 랜선이 뽑히면 send 가 침묵하는데, TCP 재전송 소진(수 분)을
//     기다리지 않고 제한 시간으로 빨리 판단한다 (C# 클라이언트와 동일한 정책)
//
// 파괴자가 소켓을 닫으므로 어떤 에러 경로에서도 fd 가 새지 않는다.

#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <poll.h>
#endif

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace bydacli {

#ifdef _WIN32
using socket_t = SOCKET;
inline constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
using socket_t = int;
inline constexpr socket_t kInvalidSocket = -1;
#endif

class TcpSocket {
public:
    TcpSocket() = default;
    ~TcpSocket() { close(); }

    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    TcpSocket(TcpSocket&& other) noexcept : fd_(other.fd_) { other.fd_ = kInvalidSocket; }
    TcpSocket& operator=(TcpSocket&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = kInvalidSocket;
        }
        return *this;
    }

    bool is_open() const noexcept { return fd_ != kInvalidSocket; }

    // 접속 + TCP_NODELAY + 논블로킹 전환까지. cancel 이 서면 즉시 중단한다.
    bool connect(const std::string& host, std::uint16_t port, int timeout_ms,
                 const std::atomic<bool>& cancel, std::string& err);

    // 전부 보낼 때까지 반복한다. stall_timeout_ms 동안 한 바이트도 못 보내면
    // 회선이 죽은 것으로 보고 실패를 돌려준다.
    bool send_all(const void* data, std::size_t len, int stall_timeout_ms,
                  const std::atomic<bool>& cancel, std::string& err);

    // 정확히 len 바이트를 받을 때까지 반복한다. idle_timeout_ms 동안
    // 한 바이트도 오지 않으면 실패. cancel 이 서면 중단한다.
    bool recv_exactly(void* data, std::size_t len, int idle_timeout_ms,
                      const std::atomic<bool>& cancel, std::string& err);

    // 지금 읽을 데이터가 있는가 (블로킹하지 않는다).
    bool readable_now() noexcept;

    void close() noexcept;

private:
    bool wait(short events, int timeout_ms) noexcept;

    socket_t fd_ = kInvalidSocket;
};

// 마지막 소켓 에러를 사람이 읽을 문자열로.
std::string last_socket_error();

}  // namespace bydacli
