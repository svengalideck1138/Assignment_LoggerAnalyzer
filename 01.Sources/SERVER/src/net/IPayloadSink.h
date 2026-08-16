// 큰 payload 를 버퍼에 모으지 않고 곧바로 흘려보내기 위한 인터페이스.
//
// UPLOAD_CHUNK 처럼 큰 프레임을 vector 에 담았다가 처리하면, 그 크기만큼
// 메모리를 잡게 된다. 이 인터페이스를 구현해 두면 FrameDecoder 가
// 도착한 바이트를 수신 버퍼에서 곧바로 넘겨준다. 복사도 없고 추가
// 할당도 없다. 과제가 요구하는 "수신하는 동시에 파싱"이 이 지점이다.

#pragma once

#include <cstddef>
#include <cstdint>

#include "net/Protocol.h"

namespace byda {

class IPayloadSink {
public:
    virtual ~IPayloadSink() = default;

    // true 를 돌려주면 이 프레임의 payload 는 버퍼링하지 않고
    // payload_chunk 로 흘려보낸다.
    virtual bool stream_payload(MsgType type, std::uint64_t length) = 0;

    // 스트리밍 중인 payload 의 조각. 이 포인터는 호출 동안만 유효하다.
    virtual void payload_chunk(const char* data, std::size_t len) = 0;
};

}  // namespace byda
