// 커맨드라인 옵션.

#pragma once

#include <cstdint>
#include <string>

namespace byda {

// 한 군데서만 고치면 --version 과 HELLO_ACK 이 함께 바뀌도록 모아 둔다.
inline constexpr const char* kServerVersion = "0.3.0";

struct Config {
    std::string bind_addr = "0.0.0.0";
    std::uint16_t port = 8088;
    int max_sessions = 4;
    int backlog = 16;

    // 세션이 놀고 있을 때의 타임아웃. 사용자가 파일 선택 대화상자를 여는
    // 동안 끊기지 않도록 넉넉히 잡는다.
    int idle_timeout_ms = 300000;

    // 업로드 중에는 데이터가 끊임없이 와야 정상이다. 이만큼 아무것도
    // 오지 않으면 네트워크가 죽은 것으로 본다.
    //
    // 유휴 타임아웃과 분리한 이유: 랜선이 뽑히면 RST 가 오지 않는다.
    // 상대는 그냥 조용해질 뿐이라, 커널이 알려주기를 기다리면
    // TCP keepalive 가 소진될 때까지 수 분이 걸린다.
    //
    // 값을 더 줄이지 않은 이유: 침묵이 곧 단절은 아니다. TCP 재전송은
    // RTO 를 1s -> 2s -> 4s 로 늘려가며 재시도하므로, 무선 구간에서
    // 패킷이 몇 개 유실되면 회선이 멀쩡해도 수 초간 멎었다가 복구된다.
    // 성급하게 끊으면 살아날 전송을 죽이게 되는데, 기다려서 치르는
    // 비용은 세션 버퍼가 잠깐 더 남는 것뿐이라 손익이 비대칭이다.
    // 대신 한도를 3등분해 중간마다 경고를 남기므로, 이상 자체는
    // 5초 안에 로그에 드러난다.
    int upload_stall_timeout_ms = 15000;

    // 세션당 고정 수신 버퍼. 생성 시 한 번만 잡고 읽기마다 재사용한다.
    // 500MB 를 받아도 이 크기를 넘지 않는 것이 메모리 상한의 핵심이다.
    std::size_t read_buffer_bytes = 256u * 1024u;

    std::string log_path;   // 비어 있으면 stderr 만
    bool verbose = false;
    bool daemonize = false;

    // --daemon 일 때만 사용한다. 중복 기동 방지(flock)와 종료 대상
    // 조회(cat 한 뒤 kill)에 쓰인다.
    std::string pid_path = "/tmp/Zhenyu_LoggerAnalyzer.pid";

    bool show_help = false;
    bool show_version = false;
};

// 성공하면 true. 실패하면 false 와 err 에 사유.
bool parse_args(int argc, char** argv, Config& cfg, std::string& err);

void print_usage(const char* argv0);
void print_version();

}  // namespace byda

