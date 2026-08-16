#!/bin/bash
# ========================================
#  강제절단 e2e 테스트 러너 (Linux)
# ========================================
#
# 빌드된 서버를 임시 포트로 띄우고 e2e_disconnect.py 를 돌린 뒤 정리한다.
#
#   bash tests/run_e2e.sh              # 서버 루트에서
#   PORT=8890 bash tests/run_e2e.sh

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_DIR="$(dirname "$SCRIPT_DIR")"
BIN="$SERVER_DIR/build/Zhenyu_LoggerAnalyzer"
PORT="${PORT:-8890}"
LOG="$(mktemp /tmp/byda_e2e_XXXXXX.log)"

if [ ! -x "$BIN" ]; then
    echo "[ERROR] server binary not found: $BIN"
    echo "        Build it first:  NO_RUN=1 bash 02.build_project_linux.sh"
    exit 1
fi
if ! command -v python3 > /dev/null 2>&1; then
    echo "[ERROR] python3 is required"
    exit 1
fi

echo "[1/3] Starting the server on port $PORT ..."
"$BIN" --port "$PORT" --verbose > "$LOG" 2>&1 &
SERVER_PID=$!

cleanup() {
    if kill -0 "$SERVER_PID" 2>/dev/null; then
        kill -TERM "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
    fi
}
trap cleanup EXIT

# 리슨할 때까지 최대 5초 기다린다.
for _ in $(seq 1 50); do
    if python3 -c "import socket;
s=socket.socket();
s.settimeout(0.1)
exit(0 if s.connect_ex(('127.0.0.1', $PORT)) == 0 else 1)" 2>/dev/null; then
        break
    fi
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "[ERROR] server exited during startup. Log:"
        tail -20 "$LOG"
        exit 1
    fi
    sleep 0.1
done

echo "[2/3] Running the disconnect regression ..."
python3 "$SCRIPT_DIR/e2e_disconnect.py" 127.0.0.1 "$PORT"
RC=$?

echo "[3/3] Stopping the server ..."
cleanup
trap - EXIT

if [ $RC -ne 0 ]; then
    echo
    echo "[ERROR] e2e failed. Server log tail:"
    tail -30 "$LOG"
    exit $RC
fi

# 서버가 SIGTERM 으로 깨끗하게 종료했는지도 확인한다.
if grep -q "event loop closed cleanly" "$LOG"; then
    echo "[ OK ] server shut down cleanly (all libuv handles released)"
fi
rm -f "$LOG"
echo "[PASS] e2e disconnect regression"
