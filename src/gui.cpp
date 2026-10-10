#include "gui.h"
#include "config.h"
#include "limiter.h"
#include "logging.h"
#include "strutil.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <memory>
#include <string>
#include <filesystem>
#include <stdexcept>

namespace {

constexpr UINT WM_APP_LOG = WM_APP + 1;
constexpr UINT WM_APP_LIMITER_FINISHED = WM_APP + 2;
constexpr UINT_PTR TIMER_STATUS_CLEAR = 1;

enum ControlId {
    ID_EDIT_LOSSES = 100,
    ID_EDIT_POLL,
    ID_EDIT_PROCESS,
    ID_EDIT_TESSERACT,
    ID_BTN_BROWSE,
    ID_CHK_DEBUG,
    ID_BTN_SAVE,
    ID_BTN_OPENFOLDER,
    ID_BTN_STARTSTOP,
    ID_EDIT_LOG,
};

struct AppState {
    HWND hwnd = nullptr;
    HWND hEditLosses = nullptr, hEditPoll = nullptr, hEditProcess = nullptr, hEditTesseract = nullptr;
    HWND hChkDebug = nullptr;
    HWND hBtnStartStop = nullptr;
    HWND hStatusLabel = nullptr;
    HWND hLogView = nullptr;
    HFONT hFont = nullptr;
    HFONT hTitleFont = nullptr;
    UINT dpi = 96;

    Config cfg;
    std::unique_ptr<Limiter> limiter;
    std::wstring configPath;
    std::wstring appDir;
};

AppState g_app;

// Layout is written in 96-DPI pixels and scaled to the monitor's DPI.
int Px(int v) {
    return MulDiv(v, static_cast<int>(g_app.dpi), 96);
}

std::wstring AppDirW() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path().wstring();
}

std::wstring GetText(HWND h) {
    int len = GetWindowTextLengthW(h);
    std::wstring s(len, L'\0');
    if (len > 0) GetWindowTextW(h, s.data(), len + 1);
    return s;
}

void SetLabel(HWND parent, HFONT font, const wchar_t* text, int x, int y, int w, int h) {
    HWND lbl = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
                                Px(x), Px(y), Px(w), Px(h), parent, nullptr, nullptr, nullptr);
    SendMessageW(lbl, WM_SETFONT, (WPARAM)font, TRUE);
}

HWND MakeEdit(HWND parent, HFONT font, int id, const wchar_t* initial, int x, int y, int w, int h) {
    HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", initial,
                              WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                              Px(x), Px(y), Px(w), Px(h), parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(e, WM_SETFONT, (WPARAM)font, TRUE);
    return e;
}

HWND MakeButton(HWND parent, HFONT font, int id, const wchar_t* text, int x, int y, int w, int h, DWORD extraStyle = 0) {
    HWND b = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | extraStyle,
                              Px(x), Px(y), Px(w), Px(h), parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(b, WM_SETFONT, (WPARAM)font, TRUE);
    return b;
}

void FlashStatus(const std::wstring& msg) {
    SetWindowTextW(g_app.hStatusLabel, msg.c_str());
    SetTimer(g_app.hwnd, TIMER_STATUS_CLEAR, 3000, nullptr);
}

void LoadFieldsFromConfig() {
    SetWindowTextW(g_app.hEditLosses, std::to_wstring(g_app.cfg.losses).c_str());
    wchar_t pollBuf[32];
    swprintf(pollBuf, 32, L"%.2f", g_app.cfg.poll);
    SetWindowTextW(g_app.hEditPoll, pollBuf);
    SetWindowTextW(g_app.hEditProcess, Utf8ToWide(g_app.cfg.process).c_str());
    SetWindowTextW(g_app.hEditTesseract, Utf8ToWide(g_app.cfg.tesseract_path).c_str());
    SendMessageW(g_app.hChkDebug, BM_SETCHECK, g_app.cfg.debug ? BST_CHECKED : BST_UNCHECKED, 0);
}

bool ReadFieldsIntoConfig() {
    try {
        int losses = std::stoi(WideToUtf8(GetText(g_app.hEditLosses)));
        double poll = std::stod(WideToUtf8(GetText(g_app.hEditPoll)));
        if (losses <= 0 || poll <= 0.0) throw std::invalid_argument("must be positive");
        g_app.cfg.losses = losses;
        g_app.cfg.poll = poll;
    } catch (...) {
        FlashStatus(L"Invalid value — loss limit must be a positive integer, poll must be a positive number.");
        return false;
    }
    g_app.cfg.process = Trim(WideToUtf8(GetText(g_app.hEditProcess)));
    g_app.cfg.tesseract_path = Trim(WideToUtf8(GetText(g_app.hEditTesseract)));
    g_app.cfg.debug = SendMessageW(g_app.hChkDebug, BM_GETCHECK, 0, 0) == BST_CHECKED;
    return true;
}

void AppendLogLine(const std::wstring& line) {
    int len = GetWindowTextLengthW(g_app.hLogView);
    if (len > 50000) {
        std::wstring text = GetText(g_app.hLogView);
        text = text.substr(text.size() - 20000);
        SetWindowTextW(g_app.hLogView, text.c_str());
        len = GetWindowTextLengthW(g_app.hLogView);
    }
    SendMessageW(g_app.hLogView, EM_SETSEL, len, len);
    std::wstring withNewline = line + L"\r\n";
    SendMessageW(g_app.hLogView, EM_REPLACESEL, FALSE, (LPARAM)withNewline.c_str());
    SendMessageW(g_app.hLogView, EM_SCROLLCARET, 0, 0);
}

void OnBrowseTesseract() {
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_app.hwnd;
    ofn.lpstrFilter = L"tesseract.exe\0tesseract.exe\0Executable Files\0*.exe\0All Files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Select tesseract.exe";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        SetWindowTextW(g_app.hEditTesseract, path);
    }
}

void OnSave() {
    if (!ReadFieldsIntoConfig()) return;
    SaveConfig(g_app.configPath, g_app.cfg);
    FlashStatus(L"Settings saved.");
}

void OnOpenFolder() {
    ShellExecuteW(nullptr, L"open", g_app.appDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void SetStartStopUi(bool running) {
    SetWindowTextW(g_app.hBtnStartStop, running ? L"Stop" : L"Start");
}

void OnToggleLimiter() {
    if (g_app.limiter && g_app.limiter->IsRunning()) {
        g_app.limiter->Stop();
        g_app.limiter.reset();
        SetStartStopUi(false);
        Logger::Instance().Info("Limiter stopped.");
        return;
    }

    if (!ReadFieldsIntoConfig()) return;
    SaveConfig(g_app.configPath, g_app.cfg);
    g_app.limiter = std::make_unique<Limiter>(g_app.cfg, [] {
        PostMessageW(g_app.hwnd, WM_APP_LIMITER_FINISHED, 0, 0);
    });
    g_app.limiter->Start();
    SetStartStopUi(true);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_app.hwnd = hwnd;

        g_app.dpi = GetDpiForWindow(hwnd);
        g_app.hFont = CreateFontW(-MulDiv(9, g_app.dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HFONT font = g_app.hFont;

        g_app.hTitleFont = CreateFontW(-MulDiv(15, g_app.dpi, 72), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HWND title = CreateWindowExW(0, L"STATIC", L"OW Loss Limiter", WS_CHILD | WS_VISIBLE,
                                      Px(16), Px(12), Px(300), Px(28), hwnd, nullptr, nullptr, nullptr);
        SendMessageW(title, WM_SETFONT, (WPARAM)g_app.hTitleFont, TRUE);

        int y = 54, rowH = 32, labelW = 140, editX = 164;
        SetLabel(hwnd, font, L"Loss Limit", 16, y, labelW, 20);
        g_app.hEditLosses = MakeEdit(hwnd, font, ID_EDIT_LOSSES, L"", editX, y - 2, 80, 24);
        SetLabel(hwnd, font, L"Close Overwatch after N consecutive losses", editX + 90, y, 320, 20);
        y += rowH;

        SetLabel(hwnd, font, L"Poll Interval (s)", 16, y, labelW, 20);
        g_app.hEditPoll = MakeEdit(hwnd, font, ID_EDIT_POLL, L"", editX, y - 2, 80, 24);
        SetLabel(hwnd, font, L"Seconds between screen checks", editX + 90, y, 320, 20);
        y += rowH;

        SetLabel(hwnd, font, L"Process Name", 16, y, labelW, 20);
        g_app.hEditProcess = MakeEdit(hwnd, font, ID_EDIT_PROCESS, L"", editX, y - 2, 180, 24);
        SetLabel(hwnd, font, L"Exact .exe name", editX + 190, y, 220, 20);
        y += rowH;

        SetLabel(hwnd, font, L"Tesseract Path", 16, y, labelW, 20);
        g_app.hEditTesseract = MakeEdit(hwnd, font, ID_EDIT_TESSERACT, L"", editX, y - 2, 300, 24);
        MakeButton(hwnd, font, ID_BTN_BROWSE, L"Browse", editX + 308, y - 3, 80, 26);
        y += rowH;

        g_app.hChkDebug = MakeButton(hwnd, font, ID_CHK_DEBUG,
                                      L"Save ROI crops to ./debug_crops/ (debug mode)",
                                      16, y, 420, 22, BS_AUTOCHECKBOX);
        y += rowH;

        MakeButton(hwnd, font, ID_BTN_SAVE, L"Save", 16, y, 100, 28);
        MakeButton(hwnd, font, ID_BTN_OPENFOLDER, L"Open App Folder", 124, y, 140, 28);
        g_app.hBtnStartStop = MakeButton(hwnd, font, ID_BTN_STARTSTOP, L"Start", 490, y, 100, 28);
        y += 36;

        g_app.hStatusLabel = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                              Px(16), Px(y), Px(560), Px(20), hwnd, nullptr, nullptr, nullptr);
        SendMessageW(g_app.hStatusLabel, WM_SETFONT, (WPARAM)font, TRUE);
        y += 28;

        SetLabel(hwnd, font, L"Log", 16, y, 100, 20);
        y += 22;
        g_app.hLogView = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                          WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE |
                                          ES_READONLY | ES_AUTOVSCROLL,
                                          Px(16), Px(y), Px(574), Px(200), hwnd, (HMENU)(INT_PTR)ID_EDIT_LOG, nullptr, nullptr);
        SendMessageW(g_app.hLogView, WM_SETFONT, (WPARAM)font, TRUE);

        g_app.appDir = AppDirW();
        g_app.configPath = g_app.appDir + L"\\config.txt";
        g_app.cfg = LoadConfig(g_app.configPath);
        LoadFieldsFromConfig();

        Logger::Instance().Init(g_app.appDir + L"\\LossLimiter.log");
        Logger::Instance().SetSink([](const std::string& line) {
            auto* w = new std::wstring(Utf8ToWide(line));
            PostMessageW(g_app.hwnd, WM_APP_LOG, 0, reinterpret_cast<LPARAM>(w));
        });
        Logger::Instance().Info("Ready.");
        return 0;
    }
    case WM_APP_LOG: {
        auto* line = reinterpret_cast<std::wstring*>(lParam);
        AppendLogLine(*line);
        delete line;
        return 0;
    }
    case WM_APP_LIMITER_FINISHED:
        if (g_app.limiter) {
            g_app.limiter->Stop();
            g_app.limiter.reset();
        }
        SetStartStopUi(false);
        return 0;
    case WM_TIMER:
        if (wParam == TIMER_STATUS_CLEAR) {
            SetWindowTextW(g_app.hStatusLabel, L"");
            KillTimer(hwnd, TIMER_STATUS_CLEAR);
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_BTN_BROWSE: OnBrowseTesseract(); return 0;
        case ID_BTN_SAVE: OnSave(); return 0;
        case ID_BTN_OPENFOLDER: OnOpenFolder(); return 0;
        case ID_BTN_STARTSTOP: OnToggleLimiter(); return 0;
        }
        break;
    case WM_DESTROY:
        if (g_app.limiter) g_app.limiter->Stop();
        Logger::Instance().SetSink(nullptr);
        if (g_app.hFont) DeleteObject(g_app.hFont);
        if (g_app.hTitleFont) DeleteObject(g_app.hTitleFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}

int RunApp(HINSTANCE hInstance, int nCmdShow) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);

    const wchar_t* kClassName = L"OwLossLimiterWindow";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    UINT dpi = GetDpiForSystem();
    RECT rect{0, 0, MulDiv(624, dpi, 96), MulDiv(520, dpi, 96)};
    AdjustWindowRectExForDpi(&rect, style, FALSE, 0, dpi);

    HWND hwnd = CreateWindowExW(0, kClassName, L"OW Loss Limiter", style,
                                 CW_USEDEFAULT, CW_USEDEFAULT,
                                 rect.right - rect.left, rect.bottom - rect.top,
                                 nullptr, nullptr, hInstance, nullptr);
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
