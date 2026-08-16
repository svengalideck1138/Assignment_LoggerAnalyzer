#!/usr/bin/env python3
"""강제절단 회귀 테스트 (과제 평가 기준 P2).

시나리오:
  1) 세션 A: 업로드 도중 RST 로 회선을 강제로 끊는다 (SO_LINGER 0).
  2) 세션 B: 같은 서버에 곧바로 다시 접속해 전체 업로드를 완주하고,
     ANALYZE_DONE 의 통계와 result.csv 가 기대값과 일치하는지 확인한다.

세션 B 가 성공하면 "전송 중 강제 절단에도 서버는 크래시 없이 자원을
정리하고 다음 세션을 정상 처리한다"가 증명된다.

의존성 없음 (표준 라이브러리만). 사용법:
  python3 e2e_disconnect.py <host> <port>
"""

import socket
import struct
import sys
import time

MAGIC = 0x42594441
VERSION = 1

HELLO = 0x01
HELLO_ACK = 0x02
UPLOAD_BEGIN = 0x10
UPLOAD_BEGIN_ACK = 0x11
UPLOAD_CHUNK = 0x12
UPLOAD_END = 0x13
UPLOAD_ACK = 0x14
ANALYZE_PROGRESS = 0x20
ANALYZE_DONE = 0x21
RESULT_BEGIN = 0x31
RESULT_CHUNK = 0x32
RESULT_END = 0x33
BYE = 0x7F
ERROR = 0x7E


def fail(msg):
    print(f"[FAIL] {msg}")
    sys.exit(1)


def frame(msg_type, payload=b""):
    return struct.pack(">IBBHQ", MAGIC, VERSION, msg_type, 0, len(payload)) + payload


def str16(s):
    b = s.encode()
    return struct.pack(">H", len(b)) + b


def recv_exactly(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            fail(f"connection closed by server after {len(buf)} of {n} bytes")
        buf += chunk
    return buf


def read_frame(sock):
    head = recv_exactly(sock, 16)
    magic, ver, msg_type, _flags, length = struct.unpack(">IBBHQ", head)
    if magic != MAGIC or ver != VERSION:
        fail(f"bad frame header: magic={magic:#x} ver={ver}")
    if length > 4 * 1024 * 1024:
        fail(f"payload too large: {length}")
    return msg_type, recv_exactly(sock, length) if length else b""


def connect_and_hello(host, port, name):
    sock = socket.create_connection((host, port), timeout=10)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    sock.sendall(frame(HELLO, struct.pack(">H", 1) + str16(name)))
    msg_type, _payload = read_frame(sock)
    if msg_type != HELLO_ACK:
        fail(f"expected HELLO_ACK, got {msg_type:#x}")
    return sock


def make_test_log():
    """정상 580줄 + 훼손 4줄. 기대값을 알고 있는 결정적 데이터."""
    lines = []
    speeds = 0.0
    accepted = 0
    for i in range(580):
        hour = 10 + (i % 3)  # 3개 시간 버킷
        spd = 100.0 + i
        lines.append(
            f"[2026-06-19_{hour:02d}:00:{i % 60:02d}.000000][11][22][{i}]"
            f" BYDA::RadarTrackNodeState: unitAddr[4181] spd[{spd:.6f}]"
        )
        speeds += spd
        accepted += 1

    corrupted = [
        "2026-06-19_10:00:00.000000][1][2][3] BYDA::RadarTrackNodeState: x",  # '[' 누락
        "[2026-06-19_10:00:00.000000] garbage garbage garbage garbage",       # 구조 없음
        "[2026-06-19_10:00:00.000000][1][2][3] BYDA::BeyondLimit: spd[888888888888888888888.88]",
        "[2026-06-19_10:00:00.000000][1][2][3] XYZW::RadarTrackNodeState: x", # 태그 훼손
    ]
    lines.extend(corrupted)

    data = ("\n".join(lines) + "\n").encode()
    expected = {
        "total": len(lines),
        "accepted": accepted,
        "rejected": len(corrupted),
        "spd_n": accepted,
        "spd_avg": speeds / accepted,
    }
    return data, expected


def upload_begin(sock, total, name):
    sock.sendall(frame(UPLOAD_BEGIN, struct.pack(">Q", total) + str16(name)))
    msg_type, payload = read_frame(sock)
    if msg_type != UPLOAD_BEGIN_ACK:
        fail(f"expected UPLOAD_BEGIN_ACK, got {msg_type:#x}")
    _upload_id, chunk_size = struct.unpack(">II", payload[:8])
    return chunk_size if 0 < chunk_size <= 4 * 1024 * 1024 else 1024 * 1024


def abort_mid_upload(host, port, data):
    """세션 A: 절반쯤 보내다 RST 로 즉사한다."""
    sock = connect_and_hello(host, port, "e2e-abort")
    chunk_size = upload_begin(sock, len(data) * 4, "abort.log")  # 총량을 크게 선언

    sent = 0
    while sent < len(data):
        n = min(chunk_size, len(data) - sent)
        sock.sendall(frame(UPLOAD_CHUNK, data[sent:sent + n]))
        sent += n

    # SO_LINGER(on, 0) 상태로 close 하면 FIN 이 아니라 RST 가 나간다.
    # "랜선이 뽑혔다"에 가장 가까운 소켓 수준의 강제 절단이다.
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
    sock.close()
    print(f"[ OK ] session A: sent {sent} bytes then reset the connection")


def full_upload(host, port, data, expected):
    """세션 B: 완주하고 결과를 검증한다."""
    sock = connect_and_hello(host, port, "e2e-full")
    chunk_size = upload_begin(sock, len(data), "full.log")

    sent = 0
    while sent < len(data):
        n = min(chunk_size, len(data) - sent)
        sock.sendall(frame(UPLOAD_CHUNK, data[sent:sent + n]))
        sent += n
    sock.sendall(frame(UPLOAD_END, struct.pack(">Q", sent)))

    # ANALYZE_DONE 까지 소화한다.
    csv_size = None
    while True:
        msg_type, payload = read_frame(sock)
        if msg_type in (UPLOAD_ACK, ANALYZE_PROGRESS):
            continue
        if msg_type == ERROR:
            code = struct.unpack(">H", payload[:2])[0]
            fail(f"server error {code} while waiting for ANALYZE_DONE")
        if msg_type == ANALYZE_DONE:
            (total_bytes, lines, accepted, rejected, _oversize,
             spd_n) = struct.unpack(">QQQQQQ", payload[:48])
            avg_len = struct.unpack(">H", payload[48:50])[0]
            avg = payload[50:50 + avg_len].decode()
            rest = payload[50 + avg_len:]
            _elapsed, _rss, csv_size = struct.unpack(">QQQ", rest[:24])
            break
        fail(f"unexpected message {msg_type:#x} while waiting for ANALYZE_DONE")

    if total_bytes != len(data):
        fail(f"consumed bytes {total_bytes} != sent {len(data)}")
    if lines != expected["total"] or accepted != expected["accepted"] \
            or rejected != expected["rejected"] or spd_n != expected["spd_n"]:
        fail(f"stats mismatch: lines={lines} accepted={accepted} "
             f"rejected={rejected} spd_n={spd_n}, expected {expected}")
    if abs(float(avg) - expected["spd_avg"]) > 1e-6:
        fail(f"spd average {avg} != expected {expected['spd_avg']:.6f}")

    # result.csv 수신.
    msg_type, payload = read_frame(sock)
    if msg_type != RESULT_BEGIN:
        fail(f"expected RESULT_BEGIN, got {msg_type:#x}")
    declared = struct.unpack(">Q", payload[:8])[0]

    csv = b""
    while True:
        msg_type, payload = read_frame(sock)
        if msg_type == RESULT_CHUNK:
            csv += payload
            continue
        if msg_type == RESULT_END:
            break
        fail(f"unexpected message {msg_type:#x} while receiving the result")

    if len(csv) != declared or declared != csv_size:
        fail(f"result size mismatch: declared={declared} csv_size={csv_size} got={len(csv)}")
    if b"HOURLY_MODULE_COUNTS" not in csv or b"spd_average" not in csv:
        fail("result.csv is missing expected sections")

    sock.sendall(frame(BYE))
    sock.close()
    print(f"[ OK ] session B: {lines} lines analyzed "
          f"(accepted={accepted}, rejected={rejected}, spd_avg={avg}), "
          f"result.csv {len(csv)} bytes verified")


def main():
    if len(sys.argv) != 3:
        fail("usage: e2e_disconnect.py <host> <port>")
    host, port = sys.argv[1], int(sys.argv[2])

    data, expected = make_test_log()
    print(f"[INFO] test log: {len(data)} bytes, "
          f"{expected['total']} lines ({expected['rejected']} corrupted)")

    abort_mid_upload(host, port, data)
    time.sleep(0.5)  # 서버가 죽은 세션을 정리할 시간

    full_upload(host, port, data, expected)
    print("[PASS] server survived a mid-upload reset and completed the next session")


if __name__ == "__main__":
    main()
