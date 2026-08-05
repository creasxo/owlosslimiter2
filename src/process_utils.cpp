#include "process_utils.h"
#include "strutil.h"
#include <tlhelp32.h>

DWORD FindProcessId(const std::string& exeName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    DWORD found = 0;

    if (Process32FirstW(snap, &entry)) {
        do {
            std::string name = WideToUtf8(entry.szExeFile);
            if (EqualsIgnoreCase(name, exeName)) {
                found = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return found;
}

bool IsProcessAlive(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    DWORD exitCode = 0;
    BOOL ok = GetExitCodeProcess(h, &exitCode);
    CloseHandle(h);
    return ok && exitCode == STILL_ACTIVE;
}

void KillProcess(DWORD pid, const LogFn& log) {
    HANDLE h = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!h) {
        log("Could not open Overwatch process to close it (already exited?).");
        return;
    }
    if (!TerminateProcess(h, 0)) {
        log("TerminateProcess failed.");
        CloseHandle(h);
        return;
    }
    DWORD wait = WaitForSingleObject(h, 5000);
    CloseHandle(h);
    log(wait == WAIT_OBJECT_0 ? "Overwatch closed." : "Close requested (still shutting down).");
}
