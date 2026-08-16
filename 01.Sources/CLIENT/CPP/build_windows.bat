@echo off
setlocal

REM ========================================
REM  Zhenyu_LoggerAnalyzer C++ Client - Windows Build
REM ========================================
REM  Visual Studio 2022 (C++ workload) 가 설치되어 있으면 된다.
REM  CMake 는 PATH 에 없으면 VS 에 번들된 것을 쓴다.

set SCRIPT_DIR=%~dp0
cd /d "%SCRIPT_DIR%"

set CMAKE=cmake
where cmake >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    set "CMAKE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)
if not exist "%CMAKE%" if "%CMAKE%" NEQ "cmake" (
    echo [ERROR] CMake not found. Install CMake or Visual Studio 2022 with C++ workload.
    exit /b 1
)

echo [1/2] Configuring (Visual Studio 2022, x64)...
"%CMAKE%" -S . -B build -G "Visual Studio 17 2022" -A x64
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] CMake configuration failed.
    exit /b 1
)

echo [2/2] Building (Release)...
"%CMAKE%" --build build --config Release --parallel
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo.
echo ========================================
echo  [SUCCESS] build\Release\Zhenyu_LoggerClient.exe
echo ========================================
endlocal
