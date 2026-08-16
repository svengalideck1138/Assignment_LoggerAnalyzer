#!/bin/bash
# ========================================
#  Zhenyu_LoggerAnalyzer C++ Client - Linux Build
# ========================================
#
# GLFW 를 소스에서 함께 빌드하므로 X11 개발 헤더와 OpenGL 이 필요하다.
# Debian/Ubuntu/RaspberryPi OS:
#   sudo apt-get install -y build-essential cmake xorg-dev libgl1-mesa-dev
#
#   bash build_linux.sh
#
# 옵션 (환경변수):
#   BUILD_TYPE=Debug     기본 Release
#   JOBS=2               기본 nproc

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 2)}"

echo "========================================"
echo " Zhenyu_LoggerAnalyzer C++ Client - Build"
echo "========================================"
echo " build type : $BUILD_TYPE"
echo " jobs       : $JOBS"
echo

# ---- 의존성 확인: 없으면 설치 명령을 안내하고 멈춘다 ----
missing=0
for t in cmake make g++; do
    if ! command -v "$t" > /dev/null 2>&1; then
        echo "[MISSING] $t"
        missing=1
    fi
done
if [ ! -e /usr/include/X11/Xlib.h ] && [ ! -e /usr/include/x11/Xlib.h ]; then
    echo "[MISSING] X11 development headers (xorg-dev)"
    missing=1
fi
if [ "$missing" -ne 0 ]; then
    echo
    echo "[ERROR] Install the build dependencies first:"
    echo "        sudo apt-get install -y build-essential cmake xorg-dev libgl1-mesa-dev"
    exit 1
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
