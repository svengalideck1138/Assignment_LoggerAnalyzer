// 시그널 처리.
//
// SIGINT / SIGTERM 은 libuv 의 uv_signal_t 로 이벤트 루프 안에서 받는다
// (TcpServer 참조). 여기서는 루프보다 먼저 해둬야 하는 것만 다룬다.

#pragma once

namespace byda {

// SIGPIPE 를 무시한다.
//
// 이 한 줄이 "전송 도중 연결이 끊겨도 크래시 금지" 요구의 절반이다.
// 클라이언트가 result.csv 를 받는 도중 강제 종료되면, 서버가 이미 닫힌
// 소켓에 write 할 때 커널이 SIGPIPE 를 보내고 기본 동작으로 프로세스가
// 즉시 죽는다. 무시로 바꾸면 write 가 EPIPE 를 돌려주므로 평범한
// 에러 처리 경로로 흘러간다.
void ignore_sigpipe();

}  // namespace byda

