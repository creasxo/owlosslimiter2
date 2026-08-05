#include "logging.h"
#include <windows.h>
#include <cstdio>
#include <mutex>
#include <ctime>

namespace {
    std::mutex g_mutex;
    FILE* g_file = nullptr;
    std::wstring g_path;
    std::function<void(const std::string&)> g_sink;

    constexpr long kMaxLogBytes = 1 * 1024 * 1024;
}

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::Init(const std::wstring& logFilePath) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_path = logFilePath;
    g_file = _wfopen(g_path.c_str(), L"a");
}

void Logger::SetSink(std::function<void(const std::string&)> sink) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sink = std::move(sink);
}

void Logger::RotateIfNeeded() {
    if (!g_file) return;
    long size = ftell(g_file);
    if (size < kMaxLogBytes) return;

    fclose(g_file);
    std::wstring oldPath = g_path + L".old";
    DeleteFileW(oldPath.c_str());
    MoveFileW(g_path.c_str(), oldPath.c_str());
    g_file = _wfopen(g_path.c_str(), L"a");
}

void Logger::Info(const std::string& msg) {
    std::function<void(const std::string&)> sinkCopy;
    std::string line;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::time_t t = std::time(nullptr);
        std::tm tmBuf;
        localtime_s(&tmBuf, &t);
        char ts[16];
        std::strftime(ts, sizeof(ts), "%H:%M:%S", &tmBuf);
        line = std::string("[") + ts + "] " + msg;

        RotateIfNeeded();
        if (g_file) {
            fprintf(g_file, "%s\n", line.c_str());
            fflush(g_file);
        }
        sinkCopy = g_sink;
    }
    if (sinkCopy) sinkCopy(line);
}
