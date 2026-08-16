// 세션이 자신을 소유한 쪽에 알림을 보내는 인터페이스.
//
// TcpSession 은 구체 서버 타입을 모른다. 이 인터페이스만 알기 때문에
// 세션 로직은 어떤 소유자(운영 서버, 테스트 하네스) 밑에서도 그대로
// 재사용할 수 있고, 서버 쪽 구현을 바꿔도 세션은 다시 컴파일만 하면 된다.

#pragma once

#include <cstdint>

namespace byda {

class ISessionCallback {
public:
    virtual ~ISessionCallback() = default;

    // 세션이 자기 핸들을 전부 닫아, 소유자가 세션 객체를 소멸시켜도
    // 안전해진 시점에 호출된다.
    virtual void on_session_closed(std::uint32_t id) = 0;
};

}  // namespace byda
