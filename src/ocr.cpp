#include "ocr.h"
#include "strutil.h"
#include <windows.h>
#include <thread>
#include <vector>

namespace {

struct Pipe {
    HANDLE rd = nullptr;
    HANDLE wr = nullptr;
};

bool MakePipe(Pipe& p, bool inheritRead, bool inheritWrite) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    if (!CreatePipe(&p.rd, &p.wr, &sa, 0)) return false;
    if (!inheritRead)  SetHandleInformation(p.rd, HANDLE_FLAG_INHERIT, 0);
    if (!inheritWrite) SetHandleInformation(p.wr, HANDLE_FLAG_INHERIT, 0);
    return true;
}

}

Detection RunOcr(const CapturedImage& img, const std::wstring& tesseractExe, std::string& error) {
    std::vector<uint8_t> png = EncodePng(img);

    Pipe stdinPipe, stdoutPipe;
    if (!MakePipe(stdinPipe, /*inheritRead=*/true, /*inheritWrite=*/false)) {
        error = "CreatePipe (stdin) failed";
        return Detection::None;
    }
    if (!MakePipe(stdoutPipe, /*inheritRead=*/false, /*inheritWrite=*/true)) {
        error = "CreatePipe (stdout) failed";
        CloseHandle(stdinPipe.rd); CloseHandle(stdinPipe.wr);
        return Detection::None;
    }

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE hNul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput  = stdinPipe.rd;
    si.hStdOutput = stdoutPipe.wr;
    si.hStdError  = hNul;

    std::wstring cmd = L"\"" + tesseractExe + L"\" - stdout --psm 6";
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                              CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(stdinPipe.rd);
    CloseHandle(stdoutPipe.wr);
    if (hNul) CloseHandle(hNul);

    if (!ok) {
        error = "Failed to launch tesseract (is the path correct? see Settings).";
        CloseHandle(stdinPipe.wr);
        CloseHandle(stdoutPipe.rd);
        return Detection::None;
    }

    std::thread writer([&] {
        const uint8_t* data = png.data();
        size_t remaining = png.size();
        while (remaining > 0) {
            DWORD written = 0;
            if (!WriteFile(stdinPipe.wr, data, static_cast<DWORD>(remaining), &written, nullptr) || written == 0) break;
            data += written;
            remaining -= written;
        }
        CloseHandle(stdinPipe.wr);
    });

    std::string output;
    char buf[4096];
    DWORD read = 0;
    while (ReadFile(stdoutPipe.rd, buf, sizeof(buf), &read, nullptr) && read > 0) {
        output.append(buf, read);
    }
    CloseHandle(stdoutPipe.rd);
    writer.join();

    DWORD waitResult = WaitForSingleObject(pi.hProcess, 10000);
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        error = "tesseract timed out and was killed.";
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (ContainsIgnoreCase(output, "DEFEAT"))  return Detection::Defeat;
    if (ContainsIgnoreCase(output, "VICTORY")) return Detection::Victory;
    return Detection::None;
}
