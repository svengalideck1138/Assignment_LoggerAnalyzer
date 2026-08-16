CMAKE_MINIMUM_REQUIRED(VERSION 3.15)

IF(POLICY CMP0091)
    CMAKE_POLICY(SET CMP0091 NEW)
ENDIF()

PROJECT(Zhenyu_LoggerAnalyzer LANGUAGES CXX)

SET(CMAKE_CXX_STANDARD 17)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)
SET(CMAKE_CXX_EXTENSIONS OFF)

IF(NOT CMAKE_BUILD_TYPE)
    SET(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
ENDIF()

# Build date (YYYYMMDD)
STRING(TIMESTAMP BUILD_DATE "%Y%m%d")
ADD_COMPILE_DEFINITIONS(BYDA_BUILD_DATE="${BUILD_DATE}")

# ============================================================
#  PLATFORM
# ============================================================
# 이 서버는 Linux 전용이다. /proc/self/status 로 상주 메모리를 읽고,
# TCP_KEEPIDLE 같은 Linux 고유 소켓 옵션과 sigaction 을 직접 쓴다.
# Windows 쪽 산출물은 01.Sources/CLIENT 의 WinForms 클라이언트다.
IF(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    MESSAGE(FATAL_ERROR
        "Zhenyu_LoggerAnalyzer targets Linux only (detected: ${CMAKE_SYSTEM_NAME}).\n"
        "The Windows deliverable is the WinForms client in 01.Sources/CLIENT/.")
ENDIF()

# 아키텍처. 교차 컴파일 시에는 -DBYDA_ARCH=aarch64 처럼 넘길 수 있다.
IF(NOT DEFINED BYDA_ARCH OR BYDA_ARCH STREQUAL "")
    SET(BYDA_ARCH ${CMAKE_SYSTEM_PROCESSOR})
ENDIF()

# ============================================================
#  THIRD PARTY
# ============================================================
SET(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty)

# ---- libuv (정적, 아키텍처별) ----
SET(LIBUV_DIR ${THIRD_PARTY_DIR}/libuv)
SET(LIBUV_INCLUDE_DIR ${LIBUV_DIR}/include)
SET(LIBUV_STATIC_LIB ${LIBUV_DIR}/lib/linux/static/${BYDA_ARCH}/libuv_a.a)

# 정적 아카이브는 빌드한 아키텍처에서만 링크된다. 한 머신에서 만든 것을
# 다른 아키텍처(x86-64 <-> aarch64 등)로 옮기면 파일은 존재하므로
# configure 는 통과하고, 한참 뒤 링크 단계에서야 깨진다.
# 그래서 경로에 아키텍처를 넣고, 없으면 여기서 바로 멈춘다.
IF(NOT EXISTS ${LIBUV_STATIC_LIB})
    MESSAGE(FATAL_ERROR
        "libuv static library for '${BYDA_ARCH}' not found:\n"
        "  ${LIBUV_STATIC_LIB}\n"
        "\n"
        "Build it on this machine first:\n"
        "  bash 3rdparty/libuv/build_linux.sh\n"
        "\n"
        "02.build_project_linux.sh does this automatically.")
ENDIF()

# ---- spdlog (헤더온리) ----
SET(SPDLOG_DIR ${THIRD_PARTY_DIR}/spdlog)
SET(SPDLOG_INCLUDE_DIR ${SPDLOG_DIR}/include)

IF(NOT EXISTS ${SPDLOG_INCLUDE_DIR}/spdlog/spdlog.h)
    MESSAGE(FATAL_ERROR
        "spdlog headers not found:\n"
        "  ${SPDLOG_INCLUDE_DIR}\n"
        "spdlog is header-only and ships with the repository; "
        "the checkout looks incomplete.")
ENDIF()

FIND_PACKAGE(Threads REQUIRED)

# ============================================================
#  SUMMARY
# ============================================================
MESSAGE(STATUS "----------------------------------------")
MESSAGE(STATUS "Project     : ${PROJECT_NAME}")
MESSAGE(STATUS "Build date  : ${BUILD_DATE}")
MESSAGE(STATUS "Build type  : ${CMAKE_BUILD_TYPE}")
MESSAGE(STATUS "System      : ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_VERSION}")
MESSAGE(STATUS "Arch        : ${BYDA_ARCH}")
MESSAGE(STATUS "Compiler    : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
MESSAGE(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
MESSAGE(STATUS "libuv       : ${LIBUV_STATIC_LIB}")
MESSAGE(STATUS "spdlog      : ${SPDLOG_INCLUDE_DIR} (header-only)")
MESSAGE(STATUS "----------------------------------------")

# ============================================================
#  SOURCE
# ============================================================
SET(BYDA_SOURCES
    src/main.cpp

    src/app/Config.cpp
    src/app/Daemon.cpp
    src/app/Signals.cpp

    src/analyze/Aggregator.cpp
    src/analyze/CsvWriter.cpp

    src/net/FrameDecoder.cpp
    src/net/ServerFactory.cpp
    src/net/TcpServer.cpp
    src/net/TcpSession.cpp

    src/parse/LogLine.cpp

    src/util/Logger.cpp
)

# ============================================================
#  EXECUTABLE
# ============================================================
ADD_EXECUTABLE(${PROJECT_NAME} ${BYDA_SOURCES})

# ============================================================
#  INCLUDE
# ============================================================
TARGET_INCLUDE_DIRECTORIES(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 서드파티 헤더는 SYSTEM 으로 넣는다. 그러지 않으면 -Wconversion 등을 켰을 때
# spdlog/fmt 내부에서 경고가 수백 줄 쏟아져 우리 코드의 경고가 묻힌다.
TARGET_INCLUDE_DIRECTORIES(${PROJECT_NAME} SYSTEM PRIVATE
    ${LIBUV_INCLUDE_DIR}
    ${SPDLOG_INCLUDE_DIR}
)

# ============================================================
#  LINK
# ============================================================
TARGET_LINK_LIBRARIES(${PROJECT_NAME} PRIVATE
    ${LIBUV_STATIC_LIB}
    Threads::Threads
    ${CMAKE_DL_LIBS}
    rt
)

# ============================================================
#  COMPILE OPTIONS
# ============================================================
TARGET_COMPILE_OPTIONS(${PROJECT_NAME} PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    -Wold-style-cast -Wcast-qual -Wdouble-promotion -Wformat=2
    -Wnon-virtual-dtor -Wnull-dereference
    $<$<CONFIG:Release>:-O2 -D_FORTIFY_SOURCE=2 -fstack-protector-strong>
    $<$<CONFIG:Debug>:-O0 -g3 -D_GLIBCXX_ASSERTIONS>
)

# 배포용 바이너리는 libstdc++/libgcc 도 정적 링크해서
# 채점 환경의 GLIBCXX 버전에 의존하지 않도록 한다.
OPTION(BYDA_STATIC_CXX "Static-link libstdc++ and libgcc" ON)
IF(BYDA_STATIC_CXX)
    TARGET_LINK_OPTIONS(${PROJECT_NAME} PRIVATE -static-libstdc++ -static-libgcc)
    MESSAGE(STATUS "Static libstdc++/libgcc : ON")
ENDIF()

# ============================================================
#  SANITIZERS
# ============================================================
# ASan 계열과 TSan 은 동시에 쓸 수 없다.
#   cmake -B build -DBYDA_SANITIZE=address
SET(BYDA_SANITIZE "off" CACHE STRING "Sanitizer: off | address | thread | undefined")
SET_PROPERTY(CACHE BYDA_SANITIZE PROPERTY STRINGS off address thread undefined)

IF(BYDA_SANITIZE STREQUAL "address")
    TARGET_COMPILE_OPTIONS(${PROJECT_NAME} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    TARGET_LINK_OPTIONS(${PROJECT_NAME} PRIVATE -fsanitize=address,undefined)
    MESSAGE(STATUS "Sanitizer   : address + undefined")
ELSEIF(BYDA_SANITIZE STREQUAL "thread")
    TARGET_COMPILE_OPTIONS(${PROJECT_NAME} PRIVATE -fsanitize=thread -fno-omit-frame-pointer -g)
    TARGET_LINK_OPTIONS(${PROJECT_NAME} PRIVATE -fsanitize=thread)
    MESSAGE(STATUS "Sanitizer   : thread")
ELSEIF(BYDA_SANITIZE STREQUAL "undefined")
    TARGET_COMPILE_OPTIONS(${PROJECT_NAME} PRIVATE -fsanitize=undefined -fno-omit-frame-pointer -g)
    TARGET_LINK_OPTIONS(${PROJECT_NAME} PRIVATE -fsanitize=undefined)
    MESSAGE(STATUS "Sanitizer   : undefined")
ELSEIF(NOT BYDA_SANITIZE STREQUAL "off")
    MESSAGE(FATAL_ERROR "Unknown BYDA_SANITIZE value: '${BYDA_SANITIZE}' "
                        "(expected: off | address | thread | undefined)")
ENDIF()

# ============================================================
#  INSTALL
# ============================================================
INCLUDE(GNUInstallDirs)
INSTALL(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
