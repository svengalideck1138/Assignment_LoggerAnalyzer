# 클라이언트 전송 엔진 헤드리스 프로브 - 독립 CMake 프로젝트.
#
# TransferClient/TcpSocket 만 컴파일한다 (ImGui/GLFW 불필요). X 서버가 없는
# 환경에서도 실서버를 상대로 취소 계약과 강제절단 견고성을 검증할 수 있고,
# PROBE_SANITIZE=ON 이면 ASan + LeakSanitizer 가 "클라이언트 쪽 누수 0" 을
# 함께 증명한다.
#
#   cmake -S tests -B tests/build -DPROBE_SANITIZE=ON
#   cmake --build tests/build
#   ./tests/build/engine_probe 127.0.0.1 8890 cancel 64

CMAKE_MINIMUM_REQUIRED(VERSION 3.15)
PROJECT(BydaClientEngineProbe LANGUAGES CXX)

SET(CMAKE_CXX_STANDARD 17)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)
SET(CMAKE_CXX_EXTENSIONS OFF)

IF(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    SET(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
ENDIF()

SET(CLIENT_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
SET(SERVER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../../SERVER/src)

IF(NOT EXISTS ${SERVER_SRC}/net/Protocol.h)
    MESSAGE(FATAL_ERROR "server Protocol.h not found: ${SERVER_SRC}/net/Protocol.h")
ENDIF()

ADD_EXECUTABLE(engine_probe
    engine_probe.cpp
    ${CLIENT_SRC}/net/Transfer.cpp
    ${CLIENT_SRC}/net/Socket.cpp
)

TARGET_INCLUDE_DIRECTORIES(engine_probe PRIVATE ${CLIENT_SRC} ${SERVER_SRC})

FIND_PACKAGE(Threads REQUIRED)
TARGET_LINK_LIBRARIES(engine_probe PRIVATE Threads::Threads)

IF(WIN32)
    TARGET_LINK_LIBRARIES(engine_probe PRIVATE ws2_32)
ENDIF()

IF(MSVC)
    TARGET_COMPILE_OPTIONS(engine_probe PRIVATE /W4 /utf-8)
ELSE()
    TARGET_COMPILE_OPTIONS(engine_probe PRIVATE -Wall -Wextra -Wshadow)
    # GCC 8 이하는 std::filesystem 링크에 stdc++fs 가 따로 필요하다.
    IF(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
        TARGET_LINK_LIBRARIES(engine_probe PRIVATE stdc++fs)
    ENDIF()
ENDIF()

# ASan + UBSan (+ LeakSanitizer). 클라이언트 엔진의 누수 0 증명용.
OPTION(PROBE_SANITIZE "Build the probe with AddressSanitizer + UBSan" OFF)
IF(PROBE_SANITIZE AND NOT MSVC)
    TARGET_COMPILE_OPTIONS(engine_probe PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    TARGET_LINK_OPTIONS(engine_probe PRIVATE -fsanitize=address,undefined)
ENDIF()
