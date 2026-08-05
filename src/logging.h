#pragma once
#include <string>
#include <functional>

class Logger {
public:
    static Logger& Instance();

    void Init(const std::wstring& logFilePath);
    void SetSink(std::function<void(const std::string&)> sink);
    void Info(const std::string& msg);

private:
    void RotateIfNeeded();
};
