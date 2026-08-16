#include "Socket.h"

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#endif

#include <chrono>
#include <memory>
#include <mutex>

namespace bydacli {
namespace {

// 취소 플래그를 이 주기로 확인한다. 너무 짧으면 CPU 를 먹고,
// 너무 길면 Cancel 버튼의 반응이 늦어진다.
constexpr int kPollSliceMs = 250;

#ifdef _WIN32
// Winsock 은 사용 전 WSAStartup 이 필요하다. 프로세스 수명 동안 한 번만.
void ensure_wsa() {
    static std::once_flag once;
    std::call_once(once, [] {
        WSADATA data;
        (void)WSAStartup(MAKEWORD(2, 2), &data);
    });
}

int last_errno() noexcept { return WSAGetLastError(); }
bool is_would_block(int e) noexcept { return e == WSAEWOULDBLOCK; }
bool is_in_progress(int e) noexcept { return e == WSAEWOULDBLOCK; }
#else
void ensure_wsa() {}
int last_errno() noexcept { return errno; }
bool is_would_block(int e) noexcept { return e == EWOULDBLOCK || e == EAGAIN; }
bool is_in_progress(int e) noexcept { return e == EINPROGRESS; }
#endif

std::string errno_text(int e) {
#ifdef _WIN32
    char buf[256] = {};
    DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, static_cast<DWORD>(e), 0, buf, sizeof(buf) - 1, nullptr);
    // FormatMessage 는 끝에 개행을 붙인다. 잘라낸다.
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
        buf[--n] = '\0';
    }
    return std::string(buf) + " (" + std::to_string(e) + ")";
#else
    return std::string(std::strerror(e)) + " (" + std::to_string(e) + ")";
#endif
}

bool set_nonblocking(socket_t fd) noexcept {
#ifdef _WIN32
    u_long on = 1;
    return ioctlsocket(fd, FIONBIO, &on) == 0;
#else
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

void close_fd(socket_t fd) noexcept {
#ifdef _WIN32
    closesocket(fd);
#else
    ::close(fd);
#endif
}

std::int64_t now_ms() noexcept {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

std::string last_socket_error() { return errno_text(last_errno()); }

bool TcpSocket::wait(short events, int timeout_ms) noexcept {
#ifdef _WIN32
    WSAPOLLFD p{};
    p.fd = fd_;
    p.events = events;
    return WSAPoll(&p, 1, timeout_ms) > 0;
#else
    struct pollfd p {};
    p.fd = fd_;
    p.events = events;
    return ::poll(&p, 1, timeout_ms) > 0;
#endif
}

bool TcpSocket::connect(const std::string& host, std::uint16_t port, int timeout_ms,
                        const std::atomic<bool>& cancel, std::string& err) {
    ensure_wsa();
    close();

    // getaddrinfo 결과는 freeaddrinfo 로 돌려줘야 한다. unique_ptr 에 맡긴다.
    struct addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* raw = nullptr;
    const std::string port_str = std::to_string(port);
    if (::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &raw) != 0 || raw == nullptr) {
        err = "cannot resolve host: " + host;
        return false;
    }
    std::unique_ptr<struct addrinfo, void (*)(struct addrinfo*)> addrs(raw, ::freeaddrinfo);

    for (struct addrinfo* ai = addrs.get(); ai != nullptr; ai = ai->ai_next) {
        fd_ = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd_ == kInvalidSocket) {
            continue;
        }

        const int one = 1;
        (void)::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY,
                           reinterpret_cast<const char*>(&one), sizeof(one));

        if (!set_nonblocking(fd_)) {
            err = "cannot set non-blocking mode: " + last_socket_error();
            close();
            return false;
        }

        // 논블로킹 connect: 곧바로 성공하거나 '진행 중'이 된다.
        const int rc = ::connect(fd_, ai->ai_addr, static_cast<int>(ai->ai_addrlen));
        if (rc == 0) {
            return true;
        }
        if (!is_in_progress(last_errno())) {
            err = "connect failed: " + last_socket_error();
            close();
            continue;  // 다음 주소(IPv6 -> IPv4 등)를 시도한다
        }

        // 쓰기 가능해질 때까지 = 접속이 끝날 때까지. 취소를 살피며 쪼개서 기다린다.
        const std::int64_t deadline = now_ms() + timeout_ms;
        for (;;) {
            if (cancel.load(std::memory_order_relaxed)) {
                err = "cancelled";
                close();
                return false;
            }
            const std::int64_t left = deadline - now_ms();
            if (left <= 0) {
                err = "connect timed out after " + std::to_string(timeout_ms) + " ms";
                close();
                break;  // 다음 주소 시도
            }
            const int slice = left < kPollSliceMs ? static_cast<int>(left) : kPollSliceMs;
            if (!wait(POLLOUT, slice)) {
                continue;  // 아직 진행 중
            }

            // 접속이 '끝났다'는 뜻일 뿐 성공이라는 뜻은 아니다. SO_ERROR 로 판별한다.
            int soerr = 0;
#ifdef _WIN32
            int optlen = sizeof(soerr);
#else
            socklen_t optlen = sizeof(soerr);
#endif
            if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR,
                             reinterpret_cast<char*>(&soerr), &optlen) != 0 ||
                soerr != 0) {
                err = "connect failed: " + errno_text(soerr != 0 ? soerr : last_errno());
                close();
                break;  // 다음 주소 시도
            }
            return true;
        }
    }

    if (err.empty()) {
        err = "no usable address for host: " + host;
    }
    return false;
}

bool TcpSocket::send_all(const void* data, std::size_t len, int stall_timeout_ms,
                         const std::atomic<bool>& cancel, std::string& err) {
    const char* p = static_cast<const char*>(data);
    std::size_t sent = 0;
    std::int64_t last_progress = now_ms();

    while (sent < len) {
        if (cancel.load(std::memory_order_relaxed)) {
            err = "cancelled";
            return false;
        }

        const int n = ::send(fd_, p + sent,
#ifdef _WIN32
                             static_cast<int>(len - sent),
#else
                             len - sent,
#endif
                             0);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            last_progress = now_ms();
            continue;
        }

        const int e = last_errno();
        if (!is_would_block(e)) {
            err = "send failed: " + errno_text(e);
            return false;
        }

        // 커널 버퍼가 가득 찼다. 자리가 날 때까지 기다리되,
        // stall_timeout_ms 동안 진전이 없으면 회선이 죽은 것으로 본다.
        if (now_ms() - last_progress >= stall_timeout_ms) {
            err = "network write stalled for " + std::to_string(stall_timeout_ms) +
                  " ms - the connection appears to be down";
            return false;
        }
        (void)wait(POLLOUT, kPollSliceMs);
    }
    return true;
}

bool TcpSocket::recv_exactly(void* data, std::size_t len, int idle_timeout_ms,
                             const std::atomic<bool>& cancel, std::string& err) {
    char* p = static_cast<char*>(data);
    std::size_t got = 0;
    std::int64_t last_progress = now_ms();

    while (got < len) {
        if (cancel.load(std::memory_order_relaxed)) {
            err = "cancelled";
            return false;
        }

        const int n = ::recv(fd_, p + got,
#ifdef _WIN32
                             static_cast<int>(len - got),
#else
                             len - got,
#endif
                             0);
        if (n > 0) {
            got += static_cast<std::size_t>(n);
            last_progress = now_ms();
            continue;
        }
        if (n == 0) {
            err = "connection closed by server after " + std::to_string(got) + " of " +
                  std::to_string(len) + " bytes";
            return false;
        }

        const int e = last_errno();
        if (!is_would_block(e)) {
            err = "recv failed: " + errno_text(e);
            return false;
        }

        if (now_ms() - last_progress >= idle_timeout_ms) {
            err = "no data from server for " + std::to_string(idle_timeout_ms) + " ms";
            return false;
        }
        (void)wait(POLLIN, kPollSliceMs);
    }
    return true;
}

bool TcpSocket::readable_now() noexcept { return is_open() && wait(POLLIN, 0); }

void TcpSocket::close() noexcept {
    if (fd_ != kInvalidSocket) {
        close_fd(fd_);
        fd_ = kInvalidSocket;
    }
}

}  // namespace bydacli
