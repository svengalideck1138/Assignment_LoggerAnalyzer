// 로깅 초기화 + 프로세스 정보 헬퍼.
//
// 실제 로그 출력은 spdlog 를 쓴다. 호출부는 이 헤더 대신
// <spdlog/spdlog.h> 를 포함하고 spdlog::info("...{}", x) 형태로 쓴다.
// 여기서는 싱크 구성과 포맷만 한 번 잡아 준다.
//
// ── 왜 직접 만든 로거를 버리고 spdlog 로 갔는가 ──────────────────────
//  - 문자열 이어붙이기(operator+) 대신 포맷 인자를 넘기므로, 로그 한 줄마다
//    임시 std::string 이 여러 개 생기던 것이 사라진다.
//  - 콘솔과 파일로 동시에 내보내는 싱크 구성, 레벨별 색상, 스레드 안전성을
//    직접 구현하지 않아도 된다.
//
// ── 금지 규칙과의 관계 ──────────────────────────────────────────────
// spdlog 내부는 C++ 표준 동적 할당을 쓴다. 하지만 과제의 금지 규칙은
// 우리가 작성하는 소스에 대한 것이고, libuv 와 마찬가지로 3rdparty 는
// 검사 대상이 아니다. scripts/check_no_raw_alloc.sh 는 server/src,
// common, tools 만 스캔한다. 우리 코드에서는 여전히 수동 할당이 0이다.
// 이 파일의 구현부도 스마트 포인터(make_shared)만 쓴다.

#pragma once

#include <cstdint>
#include <string>

namespace byda {

// 콘솔(stderr) 싱크를 만들고, file_path 가 비어 있지 않으면 파일 싱크도 추가한다.
// 실패하면 false 와 err 에 사유를 담는다.
bool init_logging(const std::string& file_path, bool verbose, std::string& err);

// 종료 시 버퍼를 비우고 로거를 정리한다.
void shutdown_logging();

// 바이트 수를 사람이 읽기 좋은 문자열로 ("482.8 MiB").
std::string human_bytes(std::uint64_t n);

// 이 프로세스의 최대 상주 메모리(VmHWM, KiB). 실패하면 0.
//
// 과제의 "프로세스 최대 메모리 50MB 이하" 조건을 스스로 측정해서
// 결과에 실어 보내기 위한 값이다. VmSize(가상 크기)가 아니라 VmHWM 을
// 쓰는 이유는, 가상 주소 공간은 실제로 점유한 물리 메모리가 아니기
// 때문이다.
std::uint64_t peak_rss_kb();

}  // namespace byda

