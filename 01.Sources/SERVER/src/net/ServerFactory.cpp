#include "net/ServerFactory.h"

#include <memory>

#include "net/TcpServer.h"

namespace byda {

std::unique_ptr<ITcpServer> ServerFactory::createTcpServer(uv_loop_t* loop, const Config& cfg) {
    return std::make_unique<TcpServer>(loop, cfg);
}

}  // namespace byda
