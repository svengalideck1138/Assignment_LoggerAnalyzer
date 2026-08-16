#include "app/Config.h"

#include <spdlog/version.h>
#include <uv.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace byda {
namespace {

// 문자열을 범위 검사와 함께 정수로. 실패하면 false.
bool to_int(const std::string& s, long min_v, long max_v, long& out) {
    if (s.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (errno != 0 || end == nullptr || *end != '\0') {
        return false;
    }
    if (v < min_v || v > max_v) {
        return false;
    }
    out = v;
    return true;
}

// 옵션이 값을 요구하는데 뒤에 인자가 없는 경우를 한 곳에서 처리한다.
bool take_value(const std::vector<std::string>& args, std::size_t& i, const std::string& name,
                std::string& out, std::string& err) {
    if (i + 1 >= args.size()) {
        err = name + " requires a value";
        return false;
    }
    ++i;
    out = args[i];
    return true;
}

}  // namespace

bool parse_args(int argc, char** argv, Config& cfg, std::string& err) {
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        std::string v;
        long n = 0;

        if (a == "-h" || a == "--help") {
            cfg.show_help = true;
            return true;
        }
        if (a == "--version") {
            cfg.show_version = true;
            return true;
        }
        if (a == "--foreground") {
            cfg.daemonize = false;
        } else if (a == "--daemon") {
            cfg.daemonize = true;
        } else if (a == "-v" || a == "--verbose") {
            cfg.verbose = true;
        } else if (a == "--bind") {
            if (!take_value(args, i, a, v, err)) return false;
            cfg.bind_addr = v;
        } else if (a == "--port" || a == "-p") {
            if (!take_value(args, i, a, v, err)) return false;
            if (!to_int(v, 1, 65535, n)) {
                err = "invalid port: " + v;
                return false;
            }
            cfg.port = static_cast<std::uint16_t>(n);
        } else if (a == "--max-sessions") {
            if (!take_value(args, i, a, v, err)) return false;
            if (!to_int(v, 1, 1024, n)) {
                err = "invalid max-sessions: " + v;
                return false;
            }
            cfg.max_sessions = static_cast<int>(n);
        } else if (a == "--idle-timeout") {
            if (!take_value(args, i, a, v, err)) return false;
            if (!to_int(v, 1000, 3600000, n)) {
                err = "invalid idle-timeout (ms): " + v;
                return false;
            }
            cfg.idle_timeout_ms = static_cast<int>(n);
        } else if (a == "--upload-timeout") {
            if (!take_value(args, i, a, v, err)) return false;
            if (!to_int(v, 1000, 600000, n)) {
                err = "invalid upload-timeout (ms): " + v;
                return false;
            }
            cfg.upload_stall_timeout_ms = static_cast<int>(n);
        } else if (a == "--log") {
            if (!take_value(args, i, a, v, err)) return false;
            cfg.log_path = v;
        } else if (a == "--pidfile") {
            if (!take_value(args, i, a, v, err)) return false;
            cfg.pid_path = v;
        } else {
            err = "unknown option: " + a;
            return false;
        }
    }

    return true;
}

void print_usage(const char* argv0) {
    std::printf(
        "Zhenyu_LoggerAnalyzer - Linux server daemon\n"
        "\n"
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  --bind <addr>         Address to bind        (default 0.0.0.0)\n"
        "  -p, --port <n>        TCP port               (default 8088)\n"
        "  --max-sessions <n>    Concurrent sessions    (default 4)\n"
        "  --idle-timeout <ms>   Idle disconnect        (default 300000)\n"
        "  --upload-timeout <ms> Stalled upload         (default 15000)\n"
        "  --log <path>          Also write to log file (default stderr only)\n"
        "  --foreground          Run in foreground      (default)\n"
        "  --daemon              Detach and run as a daemon\n"
        "  --pidfile <path>      Daemon pid file        (default /tmp/Zhenyu_LoggerAnalyzer.pid)\n"
        "  -v, --verbose         Debug level logging\n"
        "  -h, --help            Show this help\n"
        "      --version         Show version\n"
        "\n",
        argv0);
}

void print_version() {
    std::printf("Zhenyu_LoggerAnalyzer %s (libuv %s, spdlog %d.%d.%d)\n", kServerVersion, uv_version_string(),
                SPDLOG_VER_MAJOR, SPDLOG_VER_MINOR, SPDLOG_VER_PATCH);
}

}  // namespace byda
