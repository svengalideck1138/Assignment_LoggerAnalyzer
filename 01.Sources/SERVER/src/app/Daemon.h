// 고전적 double-fork 데몬화 + flock PID 파일.
//
// systemd 환경에서는 --foreground + Type=simple 이 정석이므로
// (packaging/Zhenyu_LoggerAnalyzer.service 참고) 이 모듈은 systemd 가 없는
// 환경에서 터미널을 닫아도 서버가 살아남게 하는 용도다.

#pragma once

#include <string>

namespace byda {

// 호출한 프로세스를 제어 터미널에서 완전히 분리한다.
//
// fork -> setsid -> fork -> chdir("/") -> umask(0) -> 표준 입출력을
// /dev/null 로. 성공하면 최종(손자) 프로세스에서만 true 로 돌아오고,
// 중간 프로세스들은 함수 안에서 _exit 한다. _exit 는 소멸자를 부르지
// 않으므로, 이 함수 이전에 잡아 둔 자원(PID 파일 잠금 등)을 중간
// 프로세스가 정리해 버리는 일도 없다.
bool daemonize(std::string& err);

// flock 으로 잠근 PID 파일. 잠금이 걸려 있으면 이미 다른 인스턴스가
// 떠 있다는 뜻이다. RAII: 소멸자가 잠금을 반환하고 파일을 지운다.
//
// 사용 순서가 중요하다:
//   1) daemonize() '전에' acquire() - 중복 기동을 아직 터미널이
//      보이는 시점에 알릴 수 있다. flock 잠금은 fork 를 건너
//      유지되므로 분리 후에도 유효하다.
//   2) daemonize() '후에' record_pid() - 최종 프로세스의 pid 를 적는다.
class PidFile {
public:
    PidFile() = default;
    ~PidFile();

    PidFile(const PidFile&) = delete;
    PidFile& operator=(const PidFile&) = delete;

    // 잠금에 성공하면 true. 이미 떠 있으면 false, err 에 상대 pid.
    bool acquire(const std::string& path, std::string& err);

    // 현재 프로세스의 pid 로 내용을 갈아 쓴다.
    void record_pid();

private:
    int fd_ = -1;
    std::string path_;
};

}  // namespace byda

