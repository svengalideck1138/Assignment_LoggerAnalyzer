#!/bin/bash
# ============================================================
#  libuv 1.51.0 Static Library Build Script (Linux / WSL)
# ============================================================
#  사내 build_linux.sh 는 공유 라이브러리(libuv.so)를 만든다.
#  이 스크립트는 같은 tar.gz 에서 정적 라이브러리(libuv_a.a)를 만든다.
#
#  정적으로 가는 이유:
#    제출물이 "Linux용 바이너리 실행 파일" 하나이므로, 채점자가
#    LD_LIBRARY_PATH 를 잡거나 /usr/local/lib 에 설치할 필요가 없어야 한다.
#    공유 라이브러리로 링크하면 채점 환경에서 실행 자체가 실패할 수 있다.
#
#  산출물:
#    lib/linux/static/libuv_a.a
#    include/uv.h, include/uv/*.h
#
#  사용법:
#    bash 3rdparty/libuv/build_linux.sh
# ============================================================

set -e

echo "========================================"
echo " libuv 1.51.0 Static Library Build"
echo "========================================"
echo " arch : $(uname -m)"
echo " host : $(uname -s) $(uname -r)"
echo

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

TAR_GZ_FILE="libuv-v1.51.0.tar.gz"
EXTRACT_DIR="libuv-v1.51.0"
INCLUDE_OUTPUT_DIR="include"

# 아키텍처별로 나눠 담는다. 한 머신에서 빌드한 아카이브를 다른 아키텍처로
# 그대로 가져가면 링크 단계에서 깨지는데, 경로가 같으면 그 사실이 드러나지
# 않는다. uname -m 으로 갈라두면 같은 리포에 여러 아키텍처가 공존할 수 있고,
# CMake 도 자기 아키텍처에 맞는 것만 집는다.
TARGET_ARCH="$(uname -m)"
LIB_OUTPUT_DIR="lib/linux/static/$TARGET_ARCH"

# ---------------------------------------------------------- [0/5] 의존성 확인
echo "[0/5] Checking dependencies..."
for tool in cmake make gcc tar; do
    if ! command -v "$tool" > /dev/null 2>&1; then
        echo "[ERROR] $tool is not installed!"
        echo "        sudo apt-get install build-essential cmake"
        exit 1
    fi
done
echo "[OK] cmake : $(cmake --version | head -n1)"
echo "[OK] gcc   : $(gcc --version | head -n1)"
echo

# ---------------------------------------------------------- [1/5] 압축 해제
if [ ! -f "$TAR_GZ_FILE" ]; then
    echo "[ERROR] $TAR_GZ_FILE not found in $SCRIPT_DIR"
    exit 1
fi

echo "[1/5] Extracting $TAR_GZ_FILE..."
rm -rf "$EXTRACT_DIR"
tar -xzf "$TAR_GZ_FILE"
if [ ! -d "$EXTRACT_DIR" ]; then
    echo "[ERROR] Extraction failed"
    exit 1
fi
echo "[OK] Extracted to $EXTRACT_DIR"
echo

# ---------------------------------------------------------- [2/5] 출력 디렉터리
echo "[2/5] Creating output directories..."
mkdir -p "$LIB_OUTPUT_DIR"
mkdir -p "$INCLUDE_OUTPUT_DIR/uv"
echo "[OK] $LIB_OUTPUT_DIR"
echo

# ---------------------------------------------------------- [3/5] configure
echo "[3/5] Configuring with CMake (static)..."
cd "$EXTRACT_DIR"
mkdir -p build
cd build

# -fPIC 를 켜두면 나중에 서버를 공유 라이브러리로 만들 일이 생겨도
# 정적 아카이브를 그대로 재사용할 수 있다.
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DLIBUV_BUILD_TESTS=OFF \
    -DLIBUV_BUILD_BENCH=OFF
echo "[OK] Configuration completed"
echo

# ---------------------------------------------------------- [4/5] build
echo "[4/5] Building..."
NPROC="$(nproc 2>/dev/null || echo 4)"
echo "      parallel jobs: $NPROC"
make -j"$NPROC" uv_a
echo "[OK] Build completed"
echo

# ---------------------------------------------------------- [5/5] 산출물 복사
echo "[5/5] Copying artifacts..."
# BUILD_SHARED_LIBS=OFF 로 빌드하면 정적 타깃 uv_a 의 출력 파일명이
# libuv_a.a 가 아니라 libuv.a 가 된다 (OUTPUT_NAME 이 uv 로 잡힘).
# 두 이름 모두 받아서 libuv_a.a 로 통일해 둔다.
STATIC_SRC=""
for cand in "libuv_a.a" "libuv.a"; do
    if [ -f "$cand" ]; then
        STATIC_SRC="$cand"
        break
    fi
done
if [ -z "$STATIC_SRC" ]; then
    echo "[ERROR] static archive was not produced"
    ls -l
    exit 1
fi
cp -v "$STATIC_SRC" "$SCRIPT_DIR/$LIB_OUTPUT_DIR/libuv_a.a"

cd "$SCRIPT_DIR"
cp -v "$EXTRACT_DIR/include/uv.h" "$INCLUDE_OUTPUT_DIR/"
cp -v "$EXTRACT_DIR/include/uv/"*.h "$INCLUDE_OUTPUT_DIR/uv/"
echo

echo "Cleaning up extracted sources..."
rm -rf "$EXTRACT_DIR"

echo
echo "========================================"
echo " [SUCCESS] libuv static build completed"
echo "========================================"
echo " Arch    : $TARGET_ARCH"
echo " Library : $SCRIPT_DIR/$LIB_OUTPUT_DIR/libuv_a.a"
echo " Size    : $(du -h "$SCRIPT_DIR/$LIB_OUTPUT_DIR/libuv_a.a" | cut -f1)"
echo " Headers : $SCRIPT_DIR/$INCLUDE_OUTPUT_DIR/uv.h"
echo
echo " CMake picks this up automatically via CMAKE_SYSTEM_PROCESSOR."
echo
