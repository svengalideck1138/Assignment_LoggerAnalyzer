#include "app/Signals.h"

#include <signal.h>

#include <cstring>

namespace byda {

void ignore_sigpipe() {
    // signal() 대신 sigaction() 을 쓰는 이유: signal() 의 동작이 플랫폼마다
    // 다르고(핸들러 재설치 여부 등), sigaction() 은 표준에 정확히 규정되어 있다.
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    ::sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ::sigaction(SIGPIPE, &sa, nullptr);
}

}  // namespace byda
