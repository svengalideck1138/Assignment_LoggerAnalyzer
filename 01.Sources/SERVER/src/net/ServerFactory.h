// 서버 인스턴스 생성 팩토리.
//
// 호출자는 구체 타입을 include 하지 않고 ITcpServer 로만 다룬다.
// 생성은 make_unique 로만 이루어진다 (수동 할당 없음).

#pragma once

#include <uv.h>

#include <memory>

#include "app/Config.h"
#include "net/ITcpServer.h"

namespace byda {

class ServerFactory {
public:
    static std::unique_ptr<ITcpServer> createTcpServer(uv_loop_t* loop, const Config& cfg);

private:
    ServerFactory() = default;
};

}  // namespace byda
