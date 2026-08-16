#include "util/Logger.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <array>
#include <cstdio>
#include <memory>
#include <vector>

namespace byda {

bool init_logging(const std::string& file_path, bool verbose, std::string& err) {
    try {
        std::vector<spdlog::sink_ptr> sinks;

        // 콘솔은 stderr 로 내보낸다. stdout 은 나중에 데몬 모드에서
        // 다른 용도로 쓸 수 있도록 비워 둔다.
        sinks.push_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());

        if (!file_path.empty()) {
            sinks.push_back(
                std::make_shared<spdlog::sinks::basic_file_sink_mt>(file_path, false));
        }

        auto logger = std::make_shared<spdlog::logger>("byda", sinks.begin(), sinks.end());

        // [2026-08-08 17:28:03.074] INFO  session 1 accepted from ...
        //   %e = 밀리초, %^...%$ = 레벨에만 색상 적용
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %^%-5l%$ %v");
        logger->set_level(verbose ? spdlog::level::debug : spdlog::level::info);

        // 전송 도중 프로세스가 강제 종료되어도 직전 로그가 남도록
        // info 이상은 즉시 flush 한다.
        logger->flush_on(spdlog::level::info);

        spdlog::set_default_logger(logger);
        return true;
    } catch (const spdlog::spdlog_ex& ex) {
        err = std::string("logging init failed: ") + ex.what();
        return false;
    }
}

void shutdown_logging() {
    spdlog::default_logger()->flush();
    spdlog::shutdown();
}

std::uint64_t peak_rss_kb() {
    std::FILE* f = std::fopen("/proc/self/status", "re");
    if (f == nullptr) {
        return 0;
    }

    std::uint64_t kb = 0;
    std::array<char, 256> line{};
    while (std::fgets(line.data(), static_cast<int>(line.size()), f) != nullptr) {
        unsigned long long v = 0;
        if (std::sscanf(line.data(), "VmHWM: %llu kB", &v) == 1) {
            kb = static_cast<std::uint64_t>(v);
            break;
        }
    }
    std::fclose(f);
    return kb;
}

std::string human_bytes(std::uint64_t n) {
    constexpr double kKi = 1024.0;
    const double v = static_cast<double>(n);

    std::array<char, 48> buf{};
    if (v >= kKi * kKi * kKi) {
        std::snprintf(buf.data(), buf.size(), "%.2f GiB", v / (kKi * kKi * kKi));
    } else if (v >= kKi * kKi) {
        std::snprintf(buf.data(), buf.size(), "%.1f MiB", v / (kKi * kKi));
    } else if (v >= kKi) {
        std::snprintf(buf.data(), buf.size(), "%.1f KiB", v / kKi);
    } else {
        std::snprintf(buf.data(), buf.size(), "%llu B", static_cast<unsigned long long>(n));
    }
    return std::string(buf.data());
}

}  // namespace byda
