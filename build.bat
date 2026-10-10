@echo off
setlocal
cd /d %~dp0

if not exist build mkdir build

set CXXFLAGS=-std=c++17 -O2 -Wall -municode -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00
set SRC=src\main.cpp src\gui.cpp src\limiter.cpp src\loss_tracker.cpp src\ocr.cpp src\screen_capture.cpp src\process_utils.cpp src\config.cpp src\strutil.cpp src\logging.cpp
set LIBS=-lgdi32 -luser32 -lcomdlg32 -lshell32 -lcomctl32

windres resources\app.rc -O coff -o build\app.res.o
if errorlevel 1 (
    echo Resource compile failed.
    exit /b 1
)

g++ %CXXFLAGS% -mwindows %SRC% build\app.res.o -o LossLimiter.exe %LIBS% -static -static-libgcc -static-libstdc++ -pthread
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

echo Build succeeded: LossLimiter.exe
