#!/bin/bash
# ========================================
#  Zhenyu_LoggerAnalyzer C++ Client - Linux Build
# ========================================
#
# GLFW 를 소스에서 함께 빌드하므로 X11 개발 헤더와 OpenGL 이 필요하다.
# 빠진 패키지는 이 스크립트가 직접 설치한다 (apt / dnf / pacman).
# Debian/Ubuntu/RaspberryPi OS 기준:
#   build-essential cmake xorg-dev libgl1-mesa-dev
#
#   bash build_linux.sh
#
# 옵션 (환경변수):
#   BUILD_TYPE=Debug     기본 Release
#   JOBS=2               기본 nproc
#   SKIP_DEPS=1          의존성 자동 설치를 건너뛴다 (확인만 하고 없으면 중단)

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 2)}"
SKIP_DEPS="${SKIP_DEPS:-0}"

echo "========================================"
echo " Zhenyu_LoggerAnalyzer C++ Client - Build"
echo "========================================"
echo " build type : $BUILD_TYPE"
echo " jobs       : $JOBS"
echo

# ---- 의존성 확인: 빠진 것이 있으면 설치하고 계속 진행한다 ----

# 무엇이 빠졌는지 조사해서 need_* 플래그로 남긴다.
check_deps() {
    need_toolchain=0   # g++ / make
    need_cmake=0
    need_x11=0         # X11 개발 헤더 (xorg-dev)
    need_gl=0          # OpenGL 개발 헤더 (libgl1-mesa-dev)

    command -v g++  > /dev/null 2>&1 || need_toolchain=1
    command -v make > /dev/null 2>&1 || need_toolchain=1
    command -v cmake > /dev/null 2>&1 || need_cmake=1
    [ -e /usr/include/X11/Xlib.h ] || need_x11=1
    [ -e /usr/include/GL/gl.h ]    || need_gl=1

    missing=$(( need_toolchain + need_cmake + need_x11 + need_gl ))
}

report_missing() {
    [ "$need_toolchain" -eq 1 ] && echo "[MISSING] C++ toolchain (g++, make)"
    [ "$need_cmake"     -eq 1 ] && echo "[MISSING] cmake"
    [ "$need_x11"       -eq 1 ] && echo "[MISSING] X11 development headers"
    [ "$need_gl"        -eq 1 ] && echo "[MISSING] OpenGL development headers"
    return 0
}

check_deps

if [ "$missing" -ne 0 ]; then
    report_missing
    echo

    if [ "$SKIP_DEPS" = "1" ]; then
        echo "[ERROR] SKIP_DEPS=1 - install the packages above and re-run."
        exit 1
    fi

    # 패키지 관리자 선택 (SERVER/01.PreInstallation.sh 와 같은 방식)
    PKG=""
    if command -v apt-get > /dev/null 2>&1; then
        PKG="apt"
    elif command -v dnf > /dev/null 2>&1; then
        PKG="dnf"
    elif command -v pacman > /dev/null 2>&1; then
        PKG="pacman"
    else
        echo "[ERROR] Could not find a supported package manager (apt-get / dnf / pacman)."
        echo "        Install a C++17 compiler, make, cmake, and the X11/OpenGL"
        echo "        development headers manually, then re-run this script."
        exit 1
    fi

    # sudo 가 필요한데 없으면(도커 컨테이너 등) 그냥 실행한다.
    SUDO="sudo"
    if [ "$(id -u)" = "0" ]; then
        SUDO=""
    elif ! command -v sudo > /dev/null 2>&1; then
        echo "[ERROR] Not running as root and sudo is not available."
        echo "        Install the packages above as root, then re-run this script."
        exit 1
    fi

    echo "[DEPS] Installing missing packages with $PKG (may ask for your password)..."
    case "$PKG" in
        apt)
            pkgs=""
            [ "$need_toolchain" -eq 1 ] && pkgs="$pkgs build-essential"
            [ "$need_cmake"     -eq 1 ] && pkgs="$pkgs cmake"
            [ "$need_x11"       -eq 1 ] && pkgs="$pkgs xorg-dev"
            [ "$need_gl"        -eq 1 ] && pkgs="$pkgs libgl1-mesa-dev"
            $SUDO apt-get update && $SUDO apt-get install -y $pkgs
            ;;
        dnf)
            pkgs=""
            [ "$need_toolchain" -eq 1 ] && pkgs="$pkgs gcc-c++ make"
            [ "$need_cmake"     -eq 1 ] && pkgs="$pkgs cmake"
            [ "$need_x11"       -eq 1 ] && pkgs="$pkgs libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel"
            [ "$need_gl"        -eq 1 ] && pkgs="$pkgs mesa-libGL-devel"
            $SUDO dnf install -y $pkgs
            ;;
        pacman)
            pkgs=""
            [ "$need_toolchain" -eq 1 ] && pkgs="$pkgs base-devel"
            [ "$need_cmake"     -eq 1 ] && pkgs="$pkgs cmake"
            [ "$need_x11"       -eq 1 ] && pkgs="$pkgs libx11 libxrandr libxinerama libxcursor libxi"
            [ "$need_gl"        -eq 1 ] && pkgs="$pkgs mesa"
            $SUDO pacman -Sy --needed --noconfirm $pkgs
            ;;
    esac
    rc=$?

    # 설치 후 다시 확인한다. 패키지 관리자가 성공했다고 해도 믿지 않는다.
    check_deps
    if [ "$missing" -ne 0 ]; then
        echo
        echo "[ERROR] Dependencies are still missing after the install (exit $rc):"
        report_missing
        echo "        Install them manually and re-run this script."
        exit 1
    fi
    echo "[DEPS] All build dependencies are present."
    echo
fi

echo "[1/2] Configuring..."
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
if [ $? -ne 0 ]; then
    echo "[ERROR] CMake configuration failed."
    exit 1
fi

echo "[2/2] Building..."
cmake --build "$BUILD_DIR" -j "$JOBS"
if [ $? -ne 0 ]; then
    echo "[ERROR] Build failed."
    exit 1
fi

BIN="$BUILD_DIR/Zhenyu_LoggerClient"
echo
echo "========================================"
echo " [SUCCESS] $BIN"
echo "========================================"
echo " Run it from a desktop session:  $BIN"
