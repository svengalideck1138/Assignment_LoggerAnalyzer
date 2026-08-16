#!/bin/bash
# =======================================================
# Zhenyu_LoggerAnalyzer - Daemon Registration Script
# =======================================================
#
# 빌드된 서버를 systemd 서비스로 등록해서, 부팅 시 자동으로 뜨고
# 죽으면 5초 안에 되살아나게 한다.
#
#   bash 01.PreInstallation.sh       # 1) 패키지 설치
#   bash 02.build_project_linux.sh   # 2) 빌드  (NO_RUN=1 권장)
#   bash 03.DaemonRegistration.sh    # 3) 데몬 등록  <- 이 스크립트
#
# 하는 일:
#   - packaging/ 의 서비스 템플릿을 현재 경로/계정으로 치환해 설치
#   - systemctl daemon-reload + enable --now (부팅 자동 시작 + 즉시 기동)
#   - 기동 확인 (active 상태, 포트 리슨)
#
# 옵션:
#   --port <n>    서비스가 리슨할 포트 (기본 8088)
#   --remove      등록을 해제하고 서비스 파일을 지운다
#   --status      현재 등록/동작 상태만 보여준다
#
# systemd 가 없는 환경(기본 설정의 WSL, 컨테이너)에서는 등록이 불가능하므로,
# 서버 자체의 데몬 모드를 안내하고 끝낸다:
#   ./build/Zhenyu_LoggerAnalyzer --daemon --port 8088
# =======================================================

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UNIT_NAME="Zhenyu_LoggerAnalyzer"
UNIT_SRC="$SCRIPT_DIR/packaging/Zhenyu_LoggerAnalyzer.service"
UNIT_DST="/etc/systemd/system/${UNIT_NAME}.service"
BIN="$SCRIPT_DIR/build/Zhenyu_LoggerAnalyzer"
PORT=8088
ACTION="install"

while [ $# -gt 0 ]; do
    case "$1" in
        --port)   shift; PORT="${1:-8088}" ;;
        --remove) ACTION="remove" ;;
        --status) ACTION="status" ;;
        -h|--help)
            awk 'NR>1 { if (/^#/) { sub(/^# ?/, ""); print } else { exit } }' "$0"
            exit 0
            ;;
        *) echo "[ERROR] unknown option: $1"; exit 1 ;;
    esac
    shift
done

echo "======================================================="
echo " Zhenyu_LoggerAnalyzer - Daemon Registration"
echo "======================================================="
echo

# -------------------------------------------------- sudo
SUDO="sudo"
if [ "$(id -u)" = "0" ]; then
    SUDO=""
elif ! command -v sudo > /dev/null 2>&1; then
    echo "[ERROR] Not running as root and sudo is not available."
    exit 1
fi

# -------------------------------------------------- systemd 존재 확인
# systemctl 명령이 있어도 systemd 가 PID 1 이 아니면(기본 WSL, 컨테이너)
# 서비스 등록이 동작하지 않는다. /run/systemd/system 유무가 판별 기준이다.
if [ ! -d /run/systemd/system ]; then
    echo "[INFO] This system is not running systemd (plain WSL or a container?)."
    echo
    echo "  Service registration is not possible here."
    echo "  Use the server's own daemon mode instead:"
    echo
    echo "      $BIN --daemon --port $PORT"
    echo
    echo "  Check it:   cat /tmp/Zhenyu_LoggerAnalyzer.pid"
    echo "  Stop it:    kill -TERM \$(cat /tmp/Zhenyu_LoggerAnalyzer.pid)"
    exit 0
fi

# -------------------------------------------------- --status
if [ "$ACTION" = "status" ]; then
    if [ -f "$UNIT_DST" ]; then
        echo "[INFO] Registered: $UNIT_DST"
        systemctl status "$UNIT_NAME" --no-pager || true
    else
        echo "[INFO] Not registered."
    fi
    exit 0
fi

# -------------------------------------------------- --remove
if [ "$ACTION" = "remove" ]; then
    echo "[1/2] Stopping and disabling the service..."
    $SUDO systemctl disable --now "$UNIT_NAME" 2>/dev/null
    echo "[2/2] Removing the unit file..."
    $SUDO rm -f "$UNIT_DST"
    $SUDO systemctl daemon-reload
    echo "[OK] Unregistered."
    exit 0
fi

# -------------------------------------------------- install
echo "[1/4] Checking prerequisites..."

if [ ! -x "$BIN" ]; then
    echo "[ERROR] Server binary not found: $BIN"
    echo "        Build it first:  bash 02.build_project_linux.sh"
    exit 1
fi
echo "      binary : $BIN"

if [ ! -f "$UNIT_SRC" ]; then
    echo "[ERROR] Service template not found: $UNIT_SRC"
    exit 1
fi
echo "      unit   : $UNIT_SRC"
echo "      port   : $PORT"
echo

echo "[2/4] Installing the unit (paths and account substituted)..."
RUN_USER="$(id -un)"
RUN_GROUP="$(id -gn)"

# 템플릿의 예시 경로/계정/포트를 이 머신의 실제 값으로 바꿔서 설치한다.
sed -e "s|/opt/Zhenyu_LoggerAnalyzer/01.Sources/SERVER|$SCRIPT_DIR|g" \
    -e "s|^User=.*|User=$RUN_USER|" \
    -e "s|^Group=.*|Group=$RUN_GROUP|" \
    -e "s|--port [0-9]*|--port $PORT|" \
    "$UNIT_SRC" | $SUDO tee "$UNIT_DST" > /dev/null
echo "      installed: $UNIT_DST  (User=$RUN_USER, port $PORT)"
echo

echo "[3/4] Enabling and starting..."
$SUDO systemctl daemon-reload
$SUDO systemctl enable --now "$UNIT_NAME"
sleep 1
echo

echo "[4/4] Verifying..."
STATE="$(systemctl is-active "$UNIT_NAME" 2>/dev/null)"
if [ "$STATE" = "active" ]; then
    echo "      state  : active"
else
    echo "[ERROR] Service is '$STATE'. Recent log:"
    $SUDO journalctl -u "$UNIT_NAME" -n 10 --no-pager
    exit 1
fi

if command -v ss > /dev/null 2>&1 && ss -ltn 2>/dev/null | grep -q ":$PORT "; then
    echo "      listen : port $PORT"
else
    echo "[WARN] Nothing is listening on port $PORT yet. Check:"
    echo "       journalctl -u $UNIT_NAME -f"
fi

echo
echo "======================================================="
echo " [SUCCESS] Registered and running"
echo "======================================================="
echo
echo "  Status :  systemctl status $UNIT_NAME"
echo "  Logs   :  journalctl -u $UNIT_NAME -f"
echo "  Stop   :  sudo systemctl stop $UNIT_NAME"
echo "  Remove :  bash 03.DaemonRegistration.sh --remove"
