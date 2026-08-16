CMAKE_MINIMUM_REQUIRED(VERSION 3.16)

PROJECT(Zhenyu_LoggerClient LANGUAGES C CXX)

SET(CMAKE_CXX_STANDARD 17)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)
SET(CMAKE_CXX_EXTENSIONS OFF)

IF(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    SET(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
ENDIF()

# ============================================================
#  THIRD PARTY (소스 벤더링 - 프리빌트 바이너리 없음)
# ============================================================
# 서버의 libuv 에서 배운 교훈: 정적/공유 '바이너리'를 리포에 담으면
# 다른 glibc/아키텍처에서 링크 또는 실행이 깨진다. 여기서는 imgui 와
# glfw 를 소스로 품고 항상 로컬에서 함께 빌드한다.

# ---- GLFW (창/입력, 정적) ----
SET(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
SET(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
SET(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
SET(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
# GLFW 3.4 는 Linux 에서 X11 과 Wayland 백엔드를 모두 빌드하려 한다.
# Wayland 는 wayland-scanner / wayland-protocols 가 있어야 하므로,
# 기본은 X11 만 켠다. Wayland 가 필요하면 -DGLFW_BUILD_WAYLAND=ON 으로 켠다.
IF(UNIX AND NOT APPLE)
    OPTION(GLFW_BUILD_WAYLAND "Build support for Wayland" OFF)
    OPTION(GLFW_BUILD_X11 "Build support for X11" ON)
ENDIF()
ADD_SUBDIRECTORY(3rdparty/glfw)

# ---- Dear ImGui (코어 + GLFW/OpenGL3 백엔드) ----
SET(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui)
ADD_LIBRARY(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
TARGET_INCLUDE_DIRECTORIES(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
TARGET_LINK_LIBRARIES(imgui PUBLIC glfw)
# Wayland 백엔드가 꺼져 있으면 imgui 의 GLFW 백엔드도 Wayland 네이티브 헤더를
# 요구하지 않도록 한다 (3rdparty/imgui/backends/imgui_impl_glfw.cpp 의 LOCAL PATCH).
IF(UNIX AND NOT APPLE AND NOT GLFW_BUILD_WAYLAND)
    TARGET_COMPILE_DEFINITIONS(imgui PUBLIC IMGUI_IMPL_GLFW_NO_WAYLAND)
ENDIF()

FIND_PACKAGE(OpenGL REQUIRED)
FIND_PACKAGE(Threads REQUIRED)

# ============================================================
#  EXECUTABLE
# ============================================================
ADD_EXECUTABLE(${PROJECT_NAME}
    src/main.cpp
    src/net/Socket.cpp
    src/net/Transfer.cpp
    src/ui/App.cpp
    src/ui/FileBrowser.cpp
)

# 와이어 프로토콜은 서버의 Protocol.h 가 단일 진실 소스다.
# 복사본을 두지 않고 서버 소스 트리를 SYSTEM include 로 직접 참조한다.
SET(SERVER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../SERVER/src)
IF(NOT EXISTS ${SERVER_SRC}/net/Protocol.h)
    MESSAGE(FATAL_ERROR
        "server Protocol.h not found: ${SERVER_SRC}/net/Protocol.h\n"
        "This client shares the wire protocol header with the server; "
        "keep the repository layout intact.")
ENDIF()
TARGET_INCLUDE_DIRECTORIES(${PROJECT_NAME} SYSTEM PRIVATE ${SERVER_SRC})

TARGET_LINK_LIBRARIES(${PROJECT_NAME} PRIVATE
    imgui
    OpenGL::GL
    Threads::Threads
)

# ============================================================
#  PLATFORM
# ============================================================
IF(WIN32)
    TARGET_COMPILE_DEFINITIONS(${PROJECT_NAME} PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    TARGET_LINK_LIBRARIES(${PROJECT_NAME} PRIVATE ws2_32)

    # 콘솔 창 없이 뜨는 GUI 앱으로 만들되, 진입점은 main() 을 유지한다.
    IF(MSVC)
        SET_TARGET_PROPERTIES(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
        TARGET_LINK_OPTIONS(${PROJECT_NAME} PRIVATE /ENTRY:mainCRTStartup)
    ENDIF()
ENDIF()

# GCC 8 은 std::filesystem 이 별도 라이브러리다.
IF(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
    TARGET_LINK_LIBRARIES(${PROJECT_NAME} PRIVATE stdc++fs)
ENDIF()

# ============================================================
#  COMPILE OPTIONS (1st-party 소스에만)
# ============================================================
IF(MSVC)
    TARGET_COMPILE_OPTIONS(${PROJECT_NAME} PRIVATE /W4 /utf-8)
ELSE()
    TARGET_COMPILE_OPTIONS(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wshadow)
ENDIF()

MESSAGE(STATUS "----------------------------------------")
MESSAGE(STATUS "Project     : ${PROJECT_NAME}")
MESSAGE(STATUS "System      : ${CMAKE_SYSTEM_NAME}")
MESSAGE(STATUS "Compiler    : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
MESSAGE(STATUS "Protocol    : ${SERVER_SRC}/net/Protocol.h (shared with server)")
MESSAGE(STATUS "----------------------------------------")
