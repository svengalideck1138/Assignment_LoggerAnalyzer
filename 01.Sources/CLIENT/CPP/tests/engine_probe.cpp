// 전송 엔진(TransferClient) 헤드리스 검증 프로브.
//
// GUI 없이 실서버에 붙어 전송 엔진의 자원 관리와 견고성을 검증한다.
// TransferClient 는 ImGui/GLFW 에 의존하지 않으므로 이 프로브는 X 서버가
// 없는 환경(CI, SSH)에서도 돌릴 수 있고, ASan + LeakSanitizer 로 빌드하면
// "클라이언트 쪽도 누수 0" 이 자동으로 증명된다 (핵심 평가 기준 S2).
//
// 모드:
//   cancel : 접속 -> 업로드 -> 전송 중 취소 -> ERROR(Cancelled) 확인 ->
//            같은 연결에서 재업로드 완주 -> BYE. 취소 계약의 클라이언트 측.
//   abort  : 접속 -> 업로드 -> (외부에서 서버를 강제 종료) -> 크래시 없이
//            Failed 로 전이하고 워커가 자원을 반환하는지 확인.
//            업로드가 시작되면 "READY_FOR_KILL" 을 출력하므로, 러너가
//            그 줄을 보고 서버를 죽이면 된다 (핵심 평가 기준 P2).
//
// 사용법:
//   engine_probe <host> <port> cancel|abort <size_mib>
//
// 수동 메모리 관리 금지 규칙(S1)을 그대로 따른다. 컨테이너와 RAII 뿐이다.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

#include "net/Transfer.h"

using bydacli::Phase;
using bydacli::Snapshot;
using bydacli::TransferClient;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("[FAIL] %s\n", what);
        ++g_failures;
    }
}

// 워커가 남긴 로그를 그대로 흘려보낸다. 실패 진단이 곧 로그다.
void pump_logs(TransferClient& c) {
    for (const std::string& line : c.drain_log()) {
        std::printf("       %s\n", line.c_str());
    }
}

// pred 가 참이 될 때까지 로그를 퍼올리며 기다린다.
template <class Pred>
bool wait_until(TransferClient& c, int timeout_ms, Pred&& pred) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        pump_logs(c);
        if (pred(c.snapshot())) {
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            pump_logs(c);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

// 정상 포맷 라인으로 원하는 크기의 로그 파일을 만든다.
std::string make_log_file(std::size_t size_mib) {
    const std::string path = "/tmp/byda_engine_probe.log";
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    const std::string line =
        "[2026-06-19_22:00:00.045000][4181][62750][12345] BYDA::RadarTrackNodeState: "
        "unitAddr[4181] spd[137500.000000] advDelta[62750.000000]\n";
    const std::size_t target = size_mib * 1024ull * 1024ull;
    std::size_t written = 0;
    while (written < target) {
        f << line;
        written += line.size();
    }
    return path;
}

int run_cancel_mode(TransferClient& client, const std::string& file) {
    // 1) 업로드를 걸고 데이터가 실제로 흐르기 시작할 때까지 기다린다.
    client.start_upload(file);
    check(wait_until(client, 30000,
                     [](const Snapshot& s) { return s.bytes_sent > 0; }),
          "upload made progress");

    // 2) 전송 중 취소. CANCEL 이 서버에 도착해 ERROR(Cancelled) 로
    //    응답받고 Phase::Cancelled 가 되어야 하며, 연결은 유지되어야 한다.
    client.request_cancel();
    check(wait_until(client, 15000,
                     [](const Snapshot& s) { return s.phase == Phase::Cancelled; }),
          "cancel acknowledged (Phase::Cancelled)");
    check(client.snapshot().connected, "connection stays open after the cancel");

    // 3) 재접속 없이 같은 연결로 재업로드가 완주되어야 한다.
    client.start_upload(file);
    check(wait_until(client, 120000,
                     [](const Snapshot& s) { return s.phase == Phase::Done; }),
          "re-upload on the same connection reaches Phase::Done");
    check(client.snapshot().result_valid, "ANALYZE_DONE statistics received");
    check(!client.csv_copy().empty(), "result.csv received");

    // 4) BYE 로 정리 종료. 워커가 끝나 busy 가 내려가야 한다.
    client.disconnect();
    check(wait_until(client, 10000,
                     [&client](const Snapshot&) { return !client.busy(); }),
          "worker thread exited after disconnect");
    return 0;
}

int run_abort_mode(TransferClient& client, const std::string& file) {
    // 1) 업로드를 걸고 흐르기 시작하면 러너에게 "서버를 죽여도 된다"고 알린다.
    client.start_upload(file);
    check(wait_until(client, 30000,
                     [](const Snapshot& s) { return s.bytes_sent > 0; }),
          "upload made progress");
    std::printf("READY_FOR_KILL\n");
    std::fflush(stdout);

    // 2) 서버가 사라지면 크래시 없이 Failed 로 전이해야 한다.
    //    (쓰기 정체 감시 한도 20초 + 여유)
    check(wait_until(client, 60000,
                     [](const Snapshot& s) { return s.phase == Phase::Failed; }),
          "engine reports Phase::Failed after the server vanished");
    check(!client.snapshot().error.empty(), "failure reason recorded");

    // 3) 워커가 소켓을 닫고 스스로 끝나 자원이 반환되어야 한다.
    check(wait_until(client, 10000,
                     [&client](const Snapshot&) { return !client.busy(); }),
          "worker thread exited and released its resources");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::printf("usage: engine_probe <host> <port> cancel|abort <size_mib>\n");
        return 2;
    }
    const std::string host = argv[1];
    const int port = std::atoi(argv[2]);
    const std::string mode = argv[3];
    const std::size_t size_mib = static_cast<std::size_t>(std::atoi(argv[4]));
    if (port <= 0 || port > 65535 || size_mib == 0 ||
        (mode != "cancel" && mode != "abort")) {
        std::printf("usage: engine_probe <host> <port> cancel|abort <size_mib>\n");
        return 2;
    }

    std::printf("[INFO] generating a %zu MiB log file ...\n", size_mib);
    const std::string file = make_log_file(size_mib);

    TransferClient client;
    client.connect(host, static_cast<std::uint16_t>(port), "engine-probe");
    check(wait_until(client, 15000,
                     [](const Snapshot& s) { return s.connected; }),
          "connected and handshake finished");
    if (g_failures != 0) {
        std::printf("[FAIL] engine probe (%s): could not connect\n", mode.c_str());
        return 1;
    }

    if (mode == "cancel") {
        run_cancel_mode(client, file);
    } else {
        run_abort_mode(client, file);
    }

    if (g_failures == 0) {
        std::printf("[PASS] engine probe (%s)\n", mode.c_str());
        return 0;
    }
    std::printf("[FAIL] engine probe (%s): %d check(s) failed\n", mode.c_str(),
                g_failures);
    return 1;
}
