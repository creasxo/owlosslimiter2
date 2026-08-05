#pragma once
#include <windows.h>
#include <string>
#include <functional>

DWORD FindProcessId(const std::string& exeName);

bool IsProcessAlive(DWORD pid);

using LogFn = std::function<void(const std::string&)>;
void KillProcess(DWORD pid, const LogFn& log);
