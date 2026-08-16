// TCP 서버 추상 인터페이스.
//
// main 은 이 인터페이스와 ServerFactory 만 알면 된다. 전송 계층 구현
// (현재는 libuv 기반 TcpServer)이 바뀌어도 애플리케이션 코드는 그대로다.

#pragma once

#include <cstddef>
#include <string>

namespace byda {

class ITcpServer {
public:
    virtual ~ITcpServer() = default;

    // 리슨을 시작한다. 실패하면 false 와 err 에 사유.
    virtual bool start(std::string& err) = 0;

    // 정리 종료를 시작한다. 리슨을 멈추고 세션들을 순서대로 닫는다.
    virtual void begin_shutdown(const char* reason) = 0;

    virtual std::size_t session_count() const = 0;
};

}  // namespace byda
