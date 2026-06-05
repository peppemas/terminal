@echo off
setlocal

REM ============================================================
REM  Build script for the terminal project
REM  Usage:
REM    build.bat            -> incremental build (reuses cmake-build-debug)
REM    build.bat clean      -> delete the build directory then full build
REM    build.bat rebuild    -> alias for clean
REM ============================================================

set "PROJECT_DIR=%~dp0"
cd /d "%PROJECT_DIR%"

set "BUILD_DIR=%PROJECT_DIR%cmake-build-debug"
set "EXE_PATH=%BUILD_DIR%\Debug\terminal.exe"

if /I "%1"=="clean" goto :clean
if /I "%1"=="rebuild" goto :clean
goto :configure

:clean
echo [build] Removing build directory: %BUILD_DIR%
if exist "%BUILD_DIR%" rmdir /S /Q "%BUILD_DIR%"
echo.

:configure
if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [build] Configuring with CMake ^(Debug, Visual Studio generator^)...
    cmake -S . -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=Debug
    if errorlevel 1 (
        echo.
        echo [build] CMake configuration FAILED.
        goto :fail
    )
    echo.
)

echo [build] Compiling...
cmake --build "%BUILD_DIR%" --config Debug
if errorlevel 1 (
    echo.
    echo [build] Build FAILED.
    goto :fail
)

echo.
echo [build] Build succeeded.
echo [build] Executable: %EXE_PATH%
if exist "%EXE_PATH%" (
    for %%I in ("%EXE_PATH%") do echo [build] Last modified: %%~tI
) else (
    echo [build] WARNING: expected executable not found at "%EXE_PATH%".
)
echo.
goto :done

:fail
REM echo Press any key to close...
REM pause >nul
exit /b 1

:done
REM echo Press any key to close...
REM pause >nul
exit /b 0
