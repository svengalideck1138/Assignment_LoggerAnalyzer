#!/bin/bash
# =======================================================
# Zhenyu_LoggerAnalyzer Server (prerequisites) - Setup Script
# =======================================================
#
# git clone 직후 가장 먼저 실행한다. 배포판과 아키텍처를 스스로 판별해서
# 빌드에 필요한 패키지를 설치한다.
#
#   bash 01.PreInstallation.sh
#
# 기본 동작:
#   빌드 도구 + openssh-server + ufw 를 설치하고,
#   SSH 와 서버 포트(기본 8088)를 방화벽에서 허용한 뒤 ufw 를 켠다.
#
# 옵션:
#   --with-dev-tools   검증 도구(gdb, valgrind, strace)까지 설치
#   --with-client-gui  C++ ImGui 클라이언트 빌드용 X11/OpenGL 개발 패키지까지 설치
#                      (01.Sources/CLIENT/CPP 를 이 머신에서 빌드할 때만 필요)
#   --no-ssh           openssh-server 설치와 활성화를 건너뛴다
#   --no-firewall      ufw 설치와 방화벽 설정을 통째로 건너뛴다
#   --port <n>         방화벽에서 열 서버 포트 (기본 8088)
#   --yes              확인 없이 진행
#
# 지원 배포판: Debian 계열(apt), RHEL 계열(dnf), Arch 계열(pacman)
# 물리 머신, VM, WSL, 컨테이너 어디서든 동일하게 동작한다.
# =======================================================

set -u

PORT=8088
WITH_DEV_TOOLS=0
WITH_CLIENT_GUI=0
WITH_SSH=1
WITH_FIREWALL=1
ASSUME_YES=0

while [ $# -gt 0 ]; do
    case "$1" in
        --with-dev-tools) WITH_DEV_TOOLS=1 ;;
        --with-client-gui) WITH_CLIENT_GUI=1 ;;
        --no-ssh)         WITH_SSH=0 ;;
        --no-firewall)    WITH_FIREWALL=0 ;;
        --port)           shift; PORT="${1:-8088}" ;;
        --yes|-y)         ASSUME_YES=1 ;;
        # 파일 맨 위 주석 블록을 그대로 도움말로 쓴다.
        # 줄 번호를 박아두면 헤더를 고칠 때마다 어긋나므로,
        # '#' 로 시작하지 않는 첫 줄에서 멈춘다.
        -h|--help)
            awk 'NR>1 { if (/^#/) { sub(/^# ?/, ""); print } else { exit } }' "$0"
            exit 0
            ;;
        *) echo "[ERROR] unknown option: $1"; exit 1 ;;
    esac
    shift
done

echo "======================================================="
echo " Zhenyu_LoggerAnalyzer Server (prerequisites) - Setup"
echo "======================================================="
echo

# -------------------------------------------------- 환경 판별
OS_NAME="unknown"
OS_ID="unknown"
OS_LIKE=""
if [ -r /etc/os-release ]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    OS_NAME="${PRETTY_NAME:-$NAME}"
    OS_ID="${ID:-unknown}"
    OS_LIKE="${ID_LIKE:-}"
fi

ARCH="$(uname -m)"
KERNEL="$(uname -r)"

# device-tree 를 제공하는 시스템(대부분의 ARM 보드)이면 모델명도 보여준다.
# 없는 환경에서는 그냥 생략된다.
MODEL=""
if [ -r /proc/device-tree/model ]; then
    MODEL="$(tr -d '\0' < /proc/device-tree/model)"
fi

# WSL 여부는 진단에 도움이 되므로 같이 표시한다.
VIRT=""
if grep -qiE 'microsoft|wsl' /proc/version 2>/dev/null; then
    VIRT="WSL"
elif [ -f /.dockerenv ]; then
    VIRT="container"
fi

echo "[0/4] Detecting environment"
echo "      OS      : $OS_NAME"
echo "      Kernel  : $KERNEL"
echo "      Arch    : $ARCH"
[ -n "$VIRT" ] && echo "      Runtime : $VIRT"
[ -n "$MODEL" ] && echo "      Board   : $MODEL"
echo "      CPUs    : $(nproc 2>/dev/null || echo '?')"
if [ -r /proc/meminfo ]; then
    echo "      Memory  : $(awk '/MemTotal/{printf "%.1f GiB", $2/1048576}' /proc/meminfo)"
fi
echo

# -------------------------------------------------- 패키지 관리자 선택
PKG=""
if command -v apt-get > /dev/null 2>&1; then
    PKG="apt"
elif command -v dnf > /dev/null 2>&1; then
    PKG="dnf"
elif command -v pacman > /dev/null 2>&1; then
    PKG="pacman"
else
    echo "[ERROR] Could not find a supported package manager (apt-get / dnf / pacman)."
    echo "        Install these manually and re-run 02.build_project_linux.sh:"
    echo "          a C++17 compiler (g++ 7 or newer), make, cmake 3.15+"
    exit 1
fi
echo "[INFO] Package manager: $PKG   (${OS_ID}${OS_LIKE:+, like $OS_LIKE})"
echo

# sudo 가 필요한데 없으면(도커 컨테이너 등) 그냥 실행한다.
SUDO="sudo"
if [ "$(id -u)" = "0" ]; then
    SUDO=""
elif ! command -v sudo > /dev/null 2>&1; then
    echo "[ERROR] Not running as root and sudo is not available."
    exit 1
fi

if [ "$ASSUME_YES" != "1" ]; then
    echo "The following will be installed / configured:"
    echo "  build toolchain (g++, make), cmake"
    [ "$WITH_DEV_TOOLS" = "1" ] && echo "  dev tools (gdb, valgrind, strace)"
    [ "$WITH_CLIENT_GUI" = "1" ] && echo "  client GUI build deps (X11 + OpenGL dev headers)"
    [ "$WITH_SSH" = "1" ] && echo "  openssh-server, enabled at boot"
    if [ "$WITH_FIREWALL" = "1" ]; then
        echo "  ufw, with these rules, then enabled:"
        [ "$WITH_SSH" = "1" ] && echo "      allow ssh        (so remote access is not cut off)"
        echo "      allow ${PORT}/tcp    (Zhenyu_LoggerAnalyzer server)"
    fi
    echo
    printf "Continue? [Y/n] "
    read -r reply
    case "$reply" in
        [nN]*) echo "Aborted."; exit 0 ;;
    esac
    echo
fi

# -------------------------------------------------- [1/4] 패키지 설치
echo "[1/4] Installing packages..."

# 배포판마다 패키지 이름이 다르다.
case "$PKG" in
    apt)    SSH_PKG="openssh-server"; UFW_PKG="ufw" ;;
    dnf)    SSH_PKG="openssh-server"; UFW_PKG="ufw" ;;
    pacman) SSH_PKG="openssh";        UFW_PKG="ufw" ;;
esac

EXTRA=""
[ "$WITH_SSH" = "1" ]      && EXTRA="$EXTRA $SSH_PKG"
[ "$WITH_FIREWALL" = "1" ] && EXTRA="$EXTRA $UFW_PKG"

case "$PKG" in
    apt)
        $SUDO apt-get update
        # shellcheck disable=SC2086
        $SUDO apt-get install -y build-essential cmake pkg-config $EXTRA
        if [ "$WITH_DEV_TOOLS" = "1" ]; then
            $SUDO apt-get install -y gdb valgrind strace
        fi
        if [ "$WITH_CLIENT_GUI" = "1" ]; then
            $SUDO apt-get install -y xorg-dev libgl1-mesa-dev
        fi
        ;;
    dnf)
        # shellcheck disable=SC2086
        $SUDO dnf install -y gcc-c++ make cmake pkgconf-pkg-config $EXTRA
        if [ "$WITH_DEV_TOOLS" = "1" ]; then
            $SUDO dnf install -y gdb valgrind strace
        fi
        if [ "$WITH_CLIENT_GUI" = "1" ]; then
            $SUDO dnf install -y libX11-devel libXrandr-devel libXinerama-devel \
                                 libXcursor-devel libXi-devel mesa-libGL-devel
        fi
        ;;
    pacman)
        # shellcheck disable=SC2086
        $SUDO pacman -Sy --needed --noconfirm base-devel cmake $EXTRA
        if [ "$WITH_DEV_TOOLS" = "1" ]; then
            $SUDO pacman -Sy --needed --noconfirm gdb valgrind strace
        fi
        if [ "$WITH_CLIENT_GUI" = "1" ]; then
            $SUDO pacman -Sy --needed --noconfirm libx11 libxrandr libxinerama \
                                                  libxcursor libxi mesa
        fi
        ;;
esac

if [ $? -ne 0 ]; then
    echo
    echo "[ERROR] Package installation failed."
    exit 1
fi
echo "[OK] Dependencies installed"
echo

# -------------------------------------------------- [2/4] 툴체인 검증
echo "[2/4] Verifying the toolchain..."

fail=0
need() {  # need <command> <install hint>
    if command -v "$1" > /dev/null 2>&1; then
        printf "      %-8s %s\n" "$1" "$($1 --version 2>/dev/null | head -1)"
    else
        echo "      [MISSING] $1  ($2)"
        fail=1
    fi
}
need g++    "install a C++17 compiler"
need make   "install make"
need cmake  "cmake 3.15 or newer is required"

# CMake 최소 버전 확인 (CMakeLists 는 3.15 를 요구한다)
if command -v cmake > /dev/null 2>&1; then
    CM_VER="$(cmake --version | head -1 | awk '{print $3}')"
    CM_MAJ="${CM_VER%%.*}"
    CM_REST="${CM_VER#*.}"
    CM_MIN="${CM_REST%%.*}"
    if [ "$CM_MAJ" -lt 3 ] || { [ "$CM_MAJ" -eq 3 ] && [ "$CM_MIN" -lt 15 ]; }; then
        echo "      [ERROR] cmake $CM_VER is too old; 3.15 or newer is required."
        fail=1
    fi
fi

# 실제로 C++17 이 되는지 컴파일해서 확인한다. 버전 문자열만 믿지 않는다.
TMP_SRC="$(mktemp /tmp/byda_cxx17_XXXXXX.cpp)"
TMP_BIN="${TMP_SRC%.cpp}"
cat > "$TMP_SRC" <<'EOF'
#include <memory>
#include <string_view>
int main() {
    std::string_view sv{"ok"};
    auto p = std::make_unique<int>(17);
    return (sv.size() == 2 && *p == 17) ? 0 : 1;
}
EOF
if g++ -std=c++17 "$TMP_SRC" -o "$TMP_BIN" > /dev/null 2>&1 && "$TMP_BIN"; then
    echo "      C++17    OK"
else
    echo "      [ERROR] The compiler cannot build a C++17 program."
    fail=1
fi
rm -f "$TMP_SRC" "$TMP_BIN"

if [ "$fail" -ne 0 ]; then
    echo
    echo "[ERROR] Toolchain verification failed. See the messages above."
    exit 1
fi
echo "[OK] Toolchain verified"
echo

# -------------------------------------------------- [3/4] SSH + 방화벽
echo "[3/4] SSH and firewall"

# systemd 가 없는 환경(일부 WSL, 최소 컨테이너)에서는 systemctl 이 동작하지 않는다.
HAS_SYSTEMD=0
if command -v systemctl > /dev/null 2>&1 && [ -d /run/systemd/system ]; then
    HAS_SYSTEMD=1
fi

# ---- SSH ----
if [ "$WITH_SSH" = "1" ]; then
    # 서비스 이름이 배포판마다 다르다: Debian 계열은 ssh, 그 외는 sshd.
    SSH_SVC="ssh"
    if [ "$PKG" != "apt" ]; then
        SSH_SVC="sshd"
    fi

    if [ "$HAS_SYSTEMD" = "1" ]; then
        $SUDO systemctl start "$SSH_SVC"  2>/dev/null
        $SUDO systemctl enable "$SSH_SVC" 2>/dev/null
        if systemctl is-active --quiet "$SSH_SVC" 2>/dev/null; then
            echo "      [OK] $SSH_SVC is running and enabled at boot"
        else
            echo "      [WARN] could not confirm that $SSH_SVC is running"
        fi
    else
        echo "      [SKIP] systemd not available; start sshd manually if needed"
        echo "             (e.g. sudo service ssh start)"
    fi
else
    echo "      [SKIP] SSH setup not requested (--no-ssh)"
fi

# ---- ufw ----
if [ "$WITH_FIREWALL" = "1" ]; then
    if ! command -v ufw > /dev/null 2>&1; then
        echo "      [SKIP] ufw is not available on this system"
    else
        # 순서가 중요하다. SSH 를 먼저 허용하지 않고 ufw 를 켜면
        # 원격에서 작업 중이던 접속이 그 자리에서 끊긴다.
        if [ "$WITH_SSH" = "1" ]; then
            $SUDO ufw allow ssh > /dev/null 2>&1 && echo "      [OK] allow ssh"
        fi

        $SUDO ufw allow "${PORT}/tcp" > /dev/null 2>&1 \
            && echo "      [OK] allow ${PORT}/tcp  (Zhenyu_LoggerAnalyzer)"

        # --force 를 붙이지 않으면 대화형 확인을 요구해서 스크립트가 멈춘다.
        $SUDO ufw --force enable > /dev/null 2>&1

        echo
        echo "      current rules:"
        $SUDO ufw status verbose 2>/dev/null | sed 's/^/        /'
    fi
else
    echo "      [SKIP] Firewall setup not requested (--no-firewall)"
    echo "             The server listens on TCP $PORT; open it yourself if a"
    echo "             client connects from another machine."
fi
echo

# -------------------------------------------------- [4/4] 안내
echo "[4/4] Next step"
echo
echo "  Build and run the server (this also builds the bundled libuv for $ARCH):"
echo "       bash 02.build_project_linux.sh"
if [ "$WITH_CLIENT_GUI" = "1" ]; then
    echo
    echo "  Build the C++ ImGui client (run it from a desktop session):"
    echo "       bash ../CLIENT/CPP/build_linux.sh"
fi
echo
echo "======================================================="
echo " [SUCCESS] Prerequisites are ready"
echo "======================================================="
