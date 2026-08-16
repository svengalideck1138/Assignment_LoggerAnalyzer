#!/bin/bash
# ========================================
# Zhenyu_LoggerAnalyzer Server - Build Script
# ========================================
#
# git clone 후 이 스크립트 하나로 빌드가 끝나야 한다.
# 아키텍처에 맞는 libuv 정적 라이브러리가 없으면 여기서 먼저 빌드한다.
#
#   bash 01.PreInstallation.sh     # 최초 1회 (패키지 설치)
#   bash 02.build_project_linux.sh
#
# 옵션 (환경변수):
#   BUILD_TYPE=Debug        기본 Release
#   SANITIZE=address        off | address | thread | undefined
#   JOBS=2                  기본 nproc. 메모리가 적은 환경에서는 줄인다
#   NO_RUN=1                빌드만 하고 실행하지 않는다
#   REBUILD_LIBUV=1         libuv 를 강제로 다시 빌드한다
#   PORT=8090               기본 8088. 실행 전에 그 포트를 비운다
# ========================================

set -u

echo "========================================"
echo " Zhenyu_LoggerAnalyzer Server - Build Script"
echo "========================================"
echo

# 이 스크립트는 서버 디렉터리(01.Sources/SERVER) 안에 있다.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_DIR="$SCRIPT_DIR"
BUILD_DIR="$SERVER_DIR/build"
LIBUV_DIR="$SERVER_DIR/3rdparty/libuv"

BUILD_TYPE="${BUILD_TYPE:-Release}"
SANITIZE="${SANITIZE:-off}"
ARCH="$(uname -m)"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 2)}"
LIBUV_LIB="$LIBUV_DIR/lib/linux/static/$ARCH/libuv_a.a"

echo " source     : $SERVER_DIR"
echo " arch       : $ARCH"
echo " build type : $BUILD_TYPE"
echo " sanitizer  : $SANITIZE"
echo " jobs       : $JOBS"
echo

# ================
#      Verify
# ================
echo "[0/4] Checking the toolchain..."

if [ ! -f "$SERVER_DIR/CMakeLists.txt" ]; then
    echo "[ERROR] CMakeLists.txt not found in $SERVER_DIR"
    exit 1
fi

missing=0
for t in cmake make g++; do
    if ! command -v "$t" > /dev/null 2>&1; then
        echo "      [MISSING] $t"
        missing=1
    fi
done
if [ "$missing" -ne 0 ]; then
    echo
    echo "[ERROR] Required tools are missing. Run this first:"
    echo "        bash 01.PreInstallation.sh"
    exit 1
fi

echo "      cmake : $(cmake --version | head -1)  ($(command -v cmake))"
echo "      g++   : $(g++ --version | head -1)"
echo "[OK] Toolchain present"
echo

# ================
#   libuv (3rdparty)
# ================
echo "[1/4] Checking the bundled libuv for $ARCH..."

if [ "${REBUILD_LIBUV:-0}" = "1" ]; then
    echo "      REBUILD_LIBUV=1 -> forcing a rebuild"
    rm -f "$LIBUV_LIB"
fi

if [ -f "$LIBUV_LIB" ]; then
    echo "      found: $LIBUV_LIB"
    echo "[OK] libuv is ready"
else
    echo "      not found for this architecture, building it now."
    echo "      (a prebuilt archive from another machine would fail to link)"
    echo
    if ! bash "$LIBUV_DIR/build_linux.sh"; then
        echo
        echo "[ERROR] Failed to build libuv."
        exit 1
    fi
    if [ ! -f "$LIBUV_LIB" ]; then
        echo "[ERROR] libuv build finished but the archive is missing: $LIBUV_LIB"
        exit 1
    fi
    echo "[OK] libuv built for $ARCH"
fi
echo

# ================
#      Clean
# ================
echo "[2/4] Cleaning build directory..."
if [ -d "$BUILD_DIR" ]; then
    rm -rf "$BUILD_DIR"
    if [ $? -ne 0 ]; then
        echo "[ERROR] Failed to delete build directory"
        exit 1
    fi
    echo "[OK] Build directory cleaned"
else
    echo "[INFO] Build directory does not exist, skipping clean"
fi
echo

# ================
#      Configure
# ================
echo "[3/4] Configuring with CMake..."
cmake -S "$SERVER_DIR" -B "$BUILD_DIR" -G "Unix Makefiles" \
      -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
      -DBYDA_SANITIZE="$SANITIZE"
if [ $? -ne 0 ]; then
    echo
    echo "[ERROR] CMake configuration failed!"
    echo
    echo "Please check:"
    echo "  1. CMake 3.15 or newer is installed  (cmake --version)"
    echo "  2. build-essential is installed      (g++ --version)"
    echo "  3. 3rdparty/libuv and 3rdparty/spdlog are present"
    echo
    echo "Running 'bash 01.PreInstallation.sh' fixes 1 and 2."
    exit 1
fi
echo "[OK] Configuration successful"
echo

# ================
#      Build
# ================
echo "[4/4] Building project..."
cmake --build "$BUILD_DIR" -j "$JOBS"
if [ $? -ne 0 ]; then
    echo
    echo "[ERROR] Build failed!"
    echo
    echo "If the compiler was killed without an error message, the machine most"
    echo "likely ran out of memory. Retry with fewer parallel jobs:"
    echo "    JOBS=1 bash 02.build_project_linux.sh"
    echo
    exit 1
fi
echo "[OK] Build successful"
echo

# ================
#      Summary
# ================
BIN="$BUILD_DIR/Zhenyu_LoggerAnalyzer"
echo "========================================"
echo " Build completed successfully!"
echo "========================================"
echo
echo " Executable : $BIN"
echo " Arch       : $(file -b "$BIN" 2>/dev/null | cut -d, -f1-2)"
echo " Size       : $(du -h "$BIN" | cut -f1)"
echo " Deps       :"
ldd "$BIN" 2>/dev/null | sed 's/^/              /'
MAXGLIBC="$(objdump -T "$BIN" 2>/dev/null | grep -o 'GLIBC_2\.[0-9]*' | sort -uV | tail -1)"
echo " Max GLIBC  : ${MAXGLIBC:-none}"
echo

# ================
#      Execute
# ================
PORT="${PORT:-8088}"

if [ "${NO_RUN:-0}" = "1" ]; then
    echo "[INFO] NO_RUN=1 set, skipping execution"
    echo "       Start it manually:  $BIN --port $PORT --verbose"
    exit 0
fi

# 이전 실행이 살아 있으면 "address already in use" 로 기동이 실패한다.
# 이 스크립트가 끝에서 서버를 띄우므로, 두 번 돌리면 흔히 겪는 상황이다.
# 다른 프로그램의 포트는 건드리지 않고, 이전 Zhenyu_LoggerAnalyzer 만 정리한다.
echo "Checking TCP port $PORT..."
if ss -ltn 2>/dev/null | grep -q ":$PORT "; then
    echo "      port $PORT is busy - stopping a previous Zhenyu_LoggerAnalyzer"
    pkill -f Zhenyu_LoggerAnalyzer 2>/dev/null
    sleep 1
    if ss -ltn 2>/dev/null | grep -q ":$PORT "; then
        echo
        echo "[ERROR] TCP port $PORT is held by another program."
        echo "        Inspect it with:  sudo ss -ltnp | grep :$PORT"
        echo "        Or use a different port:  PORT=8090 bash 02.build_project_linux.sh"
        exit 1
    fi
fi
echo

echo "Running application..."
echo "----------------------------------------"
"$BIN" --port "$PORT" --verbose
RC=$?
echo "----------------------------------------"
echo "Application exited with code $RC"
exit $RC
