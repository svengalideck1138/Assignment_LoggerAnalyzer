#!/bin/bash
# =======================================================
#  금지 키워드 스캔 (과제 절대 제약 S1)
# =======================================================
#
# 1st-party C++ 소스 전체(주석 포함)에서 수동 메모리 관리 토큰을 찾는다.
# 하나라도 나오면 실패한다. CI 가 매 푸시마다 이 스크립트를 돌리므로,
# "new/malloc 사용 0건"은 주장이 아니라 매 커밋마다 갱신되는 사실이 된다.
#
# 대상: 01.Sources/SERVER/src, tests / 01.Sources/CLIENT/CPP/src
# 제외: 3rdparty (벤더링된 외부 소스)

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

TARGETS=(
    "$ROOT/01.Sources/SERVER/src"
    "$ROOT/01.Sources/SERVER/tests"
    "$ROOT/01.Sources/CLIENT/CPP/src"
)

# 식별자 경계를 지키는 패턴. calloc/realloc 은 malloc 패턴에 안 걸리므로
# 각각 명시한다. delete 는 연산자/배열형 모두 잡힌다.
#
# 단, "= delete" 는 메모리 해제가 아니라 특수 멤버 함수 삭제 문법이다
# (복사 금지 - 오히려 RAII 를 지키는 관용구). 그 형태만 걷어낸 뒤
# 남는 delete 를 실패로 본다.
PATTERN='\bnew\b|\bdelete\b|\bmalloc\b|\bcalloc\b|\brealloc\b|\bfree\b'

echo "Scanning for forbidden manual-memory tokens ..."
HITS=""
for dir in "${TARGETS[@]}"; do
    if [ ! -d "$dir" ]; then
        echo "[ERROR] missing target directory: $dir"
        exit 1
    fi
    RAW="$(grep -rInE --include='*.cpp' --include='*.h' --include='*.hpp' \
                "$PATTERN" "$dir" | sed 's/=[[:space:]]*delete//g' \
              | grep -E "$PATTERN")"
    if [ -n "$RAW" ]; then
        HITS="$HITS$RAW"$'\n'
    fi
done

if [ -n "$HITS" ]; then
    printf '%s' "$HITS"
    echo
    echo "[FAIL] forbidden token found (new/delete/malloc/calloc/realloc/free)"
    exit 1
fi

echo "[PASS] zero forbidden tokens in first-party C++ sources (comments included)"
