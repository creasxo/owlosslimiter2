@echo off
setlocal
cd /d %~dp0..

if not exist build mkdir build

g++ -std=c++17 -O2 -Wall -Isrc -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 ^
    tests\test_harness.cpp src\process_utils.cpp src\screen_capture.cpp src\ocr.cpp src\config.cpp src\strutil.cpp ^
    -o build\test_harness.exe -lgdi32 -luser32 -lcomdlg32 -lshell32 -pthread
if errorlevel 1 (
    echo Test build failed.
    exit /b 1
)

build\test_harness.exe
