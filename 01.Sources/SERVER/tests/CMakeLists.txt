# 서버 유닛 테스트 - 독립 CMake 프로젝트.
#
# 서버 본체는 Linux 전용(libuv, sigaction)이지만, 파싱/집계/프로토콜
# 계층은 표준 C++ 만 쓰므로 어디서나 컴파일된다. 테스트를 별도
# 프로젝트로 두면 Windows 개발 머신에서도 push 전에 돌려볼 수 있고,
# CI 의 Linux 러너에서도 같은 파일이 그대로 돈다.
#
#   cmake -S tests -B tests/build
#   cmake --build tests/build
#   ctest --test-dir tests/build      (또는 빌드된 byda_tests 직접 실행)
#
# sanitizer 로 돌리려면:
#   cmake -S tests -B tests/build-asan -DTEST_SANITIZE=ON

CMAKE_MINIMUM_REQUIRED(VERSION 3.15)
PROJECT(BydaTests LANGUAGES CXX)

SET(CMAKE_CXX_STANDARD 17)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)
SET(CMAKE_CXX_EXTENSIONS OFF)

IF(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    SET(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
ENDIF()

SET(SRV ${CMAKE_CURRENT_SOURCE_DIR}/../src)

ADD_EXECUTABLE(byda_tests
    test_main.cpp
    test_logline.cpp
    test_linesplitter.cpp
    test_aggregator.cpp
    test_framedecoder.cpp
    test_csv.cpp

    ${SRV}/parse/LogLine.cpp
    ${SRV}/analyze/Aggregator.cpp
    ${SRV}/analyze/CsvWriter.cpp
    ${SRV}/net/FrameDecoder.cpp
)

TARGET_INCLUDE_DIRECTORIES(byda_tests PRIVATE ${SRV} ${CMAKE_CURRENT_SOURCE_DIR})

IF(MSVC)
    TARGET_COMPILE_OPTIONS(byda_tests PRIVATE /W4 /utf-8)
ELSE()
    TARGET_COMPILE_OPTIONS(byda_tests PRIVATE -Wall -Wextra -Wshadow -Wconversion)
ENDIF()

# ASan/UBSan (Linux/GCC/Clang). CI 가 이 스위치로 한 번 더 돈다.
OPTION(TEST_SANITIZE "Build tests with AddressSanitizer + UBSan" OFF)
IF(TEST_SANITIZE AND NOT MSVC)
    TARGET_COMPILE_OPTIONS(byda_tests PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    TARGET_LINK_OPTIONS(byda_tests PRIVATE -fsanitize=address,undefined)
ENDIF()

ENABLE_TESTING()
ADD_TEST(NAME unit COMMAND byda_tests)
