#include "app/Daemon.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace byda {

bool daemonize(std::string& err) {
    // 1차 fork. 부모가 나가면서 셸은 즉시 제어를 돌려받는다.
    pid_t pid = ::fork();
    if (pid < 0) {
        err = std::string("fork: ") + std::strerror(errno);
        return false;
    }
    if (pid > 0) {
        ::_exit(0);
    }

    // 자신의 세션을 만들어 제어 터미널과의 연결을 끊는다.
    if (::setsid() < 0) {
        err = std::string("setsid: ") + std::strerror(errno);
        return false;
    }

    // 2차 fork. 세션 리더 자격을 버려서, 이후 어떤 tty 를 열더라도
    // 제어 터미널이 다시 붙는 일이 없게 한다.
    pid = ::fork();
    if (pid < 0) {
        err = std::string("fork(2): ") + std::strerror(errno);
        return false;
    }
    if (pid > 0) {
        ::_exit(0);
    }

    // 작업 디렉터리가 마운트 해제를 막지 않도록 루트로 옮긴다.
    if (::chdir("/") != 0) {
        // 치명적이지 않다. 로그 파일은 절대 경로로 열기 때문이다.
    }
    ::umask(0);

    // 표준 입출력을 /dev/null 로. 이후의 기록은 파일 싱크가 맡는다.
    const int nullfd = ::open("/dev/null", O_RDWR);
    if (nullfd < 0) {
        err = std::string("open /dev/null: ") + std::strerror(errno);
        return false;
    }
    ::dup2(nullfd, STDIN_FILENO);
    ::dup2(nullfd, STDOUT_FILENO);
    ::dup2(nullfd, STDERR_FILENO);
    if (nullfd > STDERR_FILENO) {
        ::close(nullfd);
    }
    return true;
}

bool PidFile::acquire(const std::string& path, std::string& err) {
    fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd_ < 0) {
        err = "pid file " + path + ": " + std::strerror(errno);
        return false;
    }

    if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
        // 다른 인스턴스가 잠그고 있다. 그쪽 pid 를 읽어서 알려 준다.
        std::array<char, 32> buf{};
        const ssize_t n = ::read(fd_, buf.data(), buf.size() - 1);
        ::close(fd_);
        fd_ = -1;

        err = "another instance is already running";
        if (n > 0) {
            const long other = std::strtol(buf.data(), nullptr, 10);
            if (other > 0) {
                err += " (pid " + std::to_string(other) + ", " + path + ")";
            }
        }
        return false;
    }

    path_ = path;
    record_pid();
    return true;
}

void PidFile::record_pid() {
    if (fd_ < 0) {
        return;
    }
    if (::ftruncate(fd_, 0) != 0 || ::lseek(fd_, 0, SEEK_SET) < 0) {
        return;
    }
    std::array<char, 32> buf{};
    const int len =
        std::snprintf(buf.data(), buf.size(), "%ld\n", static_cast<long>(::getpid()));
    if (len > 0) {
        const ssize_t written = ::write(fd_, buf.data(), static_cast<std::size_t>(len));
        (void)written;
    }
}

PidFile::~PidFile() {
    if (fd_ >= 0) {
        // 잠금을 쥔 쪽만 여기 도달한다. 파일을 지워서 '떠 있지 않음'을
        // 명확히 하고, close 가 flock 잠금도 함께 반환한다.
        ::unlink(path_.c_str());
        ::close(fd_);
        fd_ = -1;
    }
}

}  // namespace byda
