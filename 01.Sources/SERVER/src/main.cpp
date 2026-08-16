// Zhenyu_LoggerAnalyzer - Linux server entry point.
//
// 현재 동작하는 흐름:
//   1) 클라이언트가 TCP 로 접속한다
//   2) 서버가 접속한 peer 주소를 확인해 로그에 남기고, HELLO_ACK 으로
//      "연결되었음"과 "서버가 관측한 너의 주소"를 회신한다
//   3) 클라이언트가 대용량 로그 파일 전송을 요청하고 청크로 밀어 넣는다
//   4) 서버는 받는 즉시 파싱/집계하고, 끝나면 result.csv 를 만들어 보낸다
//
// 500MB 파일을 처리하는 동안에도 상주 메모리는 수 MB 를 넘지 않는다.
// 파일도 청크도 통째로 쌓아 두지 않기 때문이다.

#include <spdlog/spdlog.h>
#include <unistd.h>
#include <uv.h>

#include <cstdio>
#include <memory>
#include <string>

#include "app/Config.h"
#include "app/Daemon.h"
#include "app/Signals.h"
#include "net/ITcpServer.h"
#include "net/ServerFactory.h"
#include "util/Logger.h"

namespace {

// uv_loop_close 전에 남아 있는 핸들을 모두 닫는다.
void close_walk_cb(uv_handle_t* handle, void* /*arg*/) {
    if (uv_is_closing(handle) == 0) {
        uv_close(handle, nullptr);
    }
}

}  // namespace

int main(int argc, char** argv) {
    byda::Config cfg;
    std::string err;

    if (!byda::parse_args(argc, argv, cfg, err)) {
        std::fprintf(stderr, "error: %s\n\n", err.c_str());
        byda::print_usage(argv[0]);
        return 2;
    }
    if (cfg.show_help) {
        byda::print_usage(argv[0]);
        return 0;
    }
    if (cfg.show_version) {
        byda::print_version();
        return 0;
    }

    // 클라이언트가 전송 도중 사라졌을 때 프로세스가 즉사하지 않도록
    // 이벤트 루프를 만들기 전에 처리해 둔다.
    byda::ignore_sigpipe();

    // 데몬화는 로거와 이벤트 루프를 만들기 '전에' 끝내야 한다.
    // fork 이후의 프로세스에서 열어야 파일 싱크의 fd 와 루프가
    // 최종 프로세스의 것이 되기 때문이다.
    byda::PidFile pidfile;
    if (cfg.daemonize) {
        // 분리되면 stderr 는 /dev/null 로 간다. 로그 파일이 없으면
        // 아무 기록도 남지 않으므로 기본 경로를 정해 준다.
        if (cfg.log_path.empty()) {
            cfg.log_path = "/tmp/Zhenyu_LoggerAnalyzer.log";
        }

        // 잠금은 아직 터미널이 보이는 지금 잡는다. 중복 기동을
        // 사용자 눈앞에서 알리기 위해서다. flock 잠금은 fork 를 건너
        // 유지되므로 분리 후에도 그대로 유효하다.
        if (!pidfile.acquire(cfg.pid_path, err)) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 6;
        }

        std::fprintf(stderr, "detaching: log=%s pidfile=%s\n", cfg.log_path.c_str(),
                     cfg.pid_path.c_str());
        if (!byda::daemonize(err)) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 6;
        }

        // 이제 pid 가 바뀌었다. 최종 프로세스의 pid 로 고쳐 적는다.
        pidfile.record_pid();
    }

    // 로거는 (데몬이라면) 분리가 끝난 뒤에 연다. 파일 싱크의 fd 가
    // 최종 프로세스 소유가 되어야 하기 때문이다.
    if (!byda::init_logging(cfg.log_path, cfg.verbose, err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 3;
    }

    spdlog::info("Zhenyu_LoggerAnalyzer starting (libuv {}, spdlog {}.{}.{})", uv_version_string(),
                 SPDLOG_VER_MAJOR, SPDLOG_VER_MINOR, SPDLOG_VER_PATCH);
    if (cfg.daemonize) {
        spdlog::info("daemonized, pid {} (pidfile {})", static_cast<long>(::getpid()),
                     cfg.pid_path);
    }

    // 루프 객체를 스택에 둔다. uv_default_loop() 대신 명시적으로 만들어야
    // 종료 시점에 uv_loop_close 로 자원 반환을 확인할 수 있다.
    uv_loop_t loop;
    int rc = uv_loop_init(&loop);
    if (rc != 0) {
        spdlog::error("uv_loop_init: {}", uv_strerror(rc));
        byda::shutdown_logging();
        return 4;
    }

    int exit_code = 0;
    {
        // 애플리케이션은 인터페이스로만 서버를 다룬다. 전송 계층 구현이
        // 바뀌어도 이 파일은 그대로다.
        std::unique_ptr<byda::ITcpServer> server =
            byda::ServerFactory::createTcpServer(&loop, cfg);
        if (!server->start(err)) {
            spdlog::error("{}", err);
            exit_code = 5;
        } else {
            uv_run(&loop, UV_RUN_DEFAULT);
            spdlog::info("event loop finished");
        }
    }

    // 남은 핸들을 닫고 루프를 한 번 더 돌려 close 콜백을 소화한다.
    uv_walk(&loop, close_walk_cb, nullptr);
    uv_run(&loop, UV_RUN_DEFAULT);

    rc = uv_loop_close(&loop);
    if (rc != 0) {
        // 루프가 깨끗하게 닫히지 않았다면 어딘가 핸들이 남아 있다는 뜻이다.
        spdlog::warn("uv_loop_close: {}", uv_strerror(rc));
    } else {
        spdlog::info("event loop closed cleanly, all handles released");
    }

    spdlog::info("Zhenyu_LoggerAnalyzer stopped");
    byda::shutdown_logging();
    return exit_code;
}
