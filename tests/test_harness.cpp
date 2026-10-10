// Test runner for the parts of LossLimiter that can be checked without a game
// running. Usage: test_harness.exe [path\to\tesseract.exe]
#include "process_utils.h"
#include "screen_capture.h"
#include "ocr.h"
#include "config.h"
#include "loss_tracker.h"
#include "strutil.h"
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& name, const std::string& detail = "") {
    ++g_checks;
    if (!ok) ++g_failures;
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << name;
    if (!detail.empty()) std::cout << " (" << detail << ")";
    std::cout << "\n";
}

using Event = LossTracker::Event;

// Feeds the same detection for `seconds`, one poll per second, starting at t.
// Returns the last non-None event, and advances t.
Event Feed(LossTracker& tracker, Detection d, int seconds, double& t) {
    Event last = Event::None;
    for (int i = 0; i < seconds; ++i, t += 1.0) {
        Event e = tracker.Update(d, t);
        if (e != Event::None) last = e;
    }
    return last;
}

// One full match result: banner on screen for 6s, then back to gameplay.
Event PlayMatch(LossTracker& tracker, Detection result, double& t) {
    Event e = Feed(tracker, result, 6, t);
    Event after = Feed(tracker, Detection::None, 10, t);
    return e != Event::None ? e : after;
}

void TestLossTracker() {
    std::cout << "LossTracker\n";
    double t = 0;

    {
        LossTracker tr(3);
        tr.Update(Detection::Defeat, t++);
        Feed(tr, Detection::None, 20, t);
        Check(tr.Losses() == 0, "a single DEFEAT frame is ignored (debounce)");
    }
    {
        LossTracker tr(3);
        for (int i = 0; i < 10; ++i) {
            tr.Update(i % 2 ? Detection::Defeat : Detection::Victory, t++);
        }
        Feed(tr, Detection::None, 20, t);
        Check(tr.Losses() == 0, "alternating DEFEAT/VICTORY never settles");
    }
    {
        LossTracker tr(3);
        Event e = PlayMatch(tr, Detection::Defeat, t);
        Check(e == Event::Loss && tr.Losses() == 1, "a defeat counts once",
              "losses=" + std::to_string(tr.Losses()));
    }
    {
        LossTracker tr(3);
        Feed(tr, Detection::Defeat, 30, t);
        Check(tr.Losses() == 1, "a banner left up for 30s counts once",
              "losses=" + std::to_string(tr.Losses()));
    }
    {
        LossTracker tr(3);
        PlayMatch(tr, Detection::Defeat, t);
        PlayMatch(tr, Detection::Defeat, t);
        Event e = PlayMatch(tr, Detection::Victory, t);
        Check(e == Event::Win && tr.Losses() == 0, "a win resets the streak");
    }
    {
        LossTracker tr(3);
        Event a = PlayMatch(tr, Detection::Defeat, t);
        Event b = PlayMatch(tr, Detection::Defeat, t);
        Event c = PlayMatch(tr, Detection::Defeat, t);
        Check(a == Event::Loss && b == Event::Loss && c == Event::LimitReached,
              "third loss in a row reaches the limit of 3");
    }
    {
        LossTracker tr(1);
        Check(PlayMatch(tr, Detection::Defeat, t) == Event::LimitReached, "limit of 1 triggers on the first loss");
    }
}

void TestConfig() {
    std::cout << "Config\n";
    auto dir = std::filesystem::temp_directory_path();
    std::wstring path = (dir / L"losslimiter_test_config.txt").wstring();

    Config missing = LoadConfig(path + L".does-not-exist");
    Check(missing.losses == 3 && missing.process == "Overwatch.exe", "missing file gives defaults");

    Config cfg;
    cfg.losses = 5;
    cfg.poll = 1.5;
    cfg.process = "Game.exe";
    cfg.tesseract_path = "D:\\Tools\\tesseract.exe";
    cfg.debug = true;
    SaveConfig(path, cfg);
    Config back = LoadConfig(path);
    Check(back.losses == 5 && back.poll == 1.5 && back.process == "Game.exe" &&
          back.tesseract_path == cfg.tesseract_path && back.debug,
          "save then load round-trips every field");

    FILE* f = _wfopen(path.c_str(), L"wb");
    fputs("# comment\r\nlosses = abc\r\n  poll=0.5  \r\nnot a setting\r\ndebug=TRUE\r\n", f);
    fclose(f);
    Config messy = LoadConfig(path);
    Check(messy.losses == 3 && messy.poll == 0.5 && messy.debug,
          "bad values and junk lines are skipped, CRLF and spaces tolerated");

    DeleteFileW(path.c_str());
}

void TestStrings() {
    std::cout << "String helpers\n";
    Check(Trim("  a b \r\n") == "a b", "Trim");
    Check(EqualsIgnoreCase("Overwatch.exe", "OVERWATCH.EXE"), "EqualsIgnoreCase");
    Check(ContainsIgnoreCase("xx\nDefeat\n", "DEFEAT"), "ContainsIgnoreCase");
    Check(WideToUtf8(Utf8ToWide("caf\xC3\xA9")) == "caf\xC3\xA9", "UTF-8 round trip");
}

void TestProcessLookup() {
    std::cout << "Process lookup\n";
    char self[MAX_PATH];
    GetModuleFileNameA(nullptr, self, MAX_PATH);
    std::string selfName = std::filesystem::path(self).filename().string();

    Check(FindProcessId(selfName) == GetCurrentProcessId(), "finds its own process", selfName);
    Check(FindProcessId("NoSuchProcess12345.exe") == 0, "unknown process returns 0");

    std::string upper = selfName;
    for (char& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    Check(FindProcessId(upper) != 0, "lookup is case-insensitive");
}

CapturedImage RenderTextImage(const wchar_t* text) {
    int w = 400, h = 120;
    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);
    HBITMAP bmp = CreateCompatibleBitmap(screenDc, w, h);
    HGDIOBJ old = SelectObject(memDc, bmp);

    RECT r{0, 0, w, h};
    FillRect(memDc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
    HFONT font = CreateFontW(48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(memDc, font);
    SetBkMode(memDc, TRANSPARENT);
    SetTextColor(memDc, RGB(0, 0, 0));
    DrawTextW(memDc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(bi);
    bi.biWidth = w;
    bi.biHeight = -h;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    std::vector<uint8_t> bgra(static_cast<size_t>(w) * h * 4);
    GetDIBits(memDc, bmp, 0, h, bgra.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    SelectObject(memDc, oldFont);
    SelectObject(memDc, old);
    DeleteObject(font);
    DeleteObject(bmp);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);

    CapturedImage img;
    img.width = w;
    img.height = h;
    img.gray.resize(static_cast<size_t>(w) * h);
    for (int i = 0; i < w * h; ++i) {
        uint8_t b = bgra[i * 4 + 0], g = bgra[i * 4 + 1], r2 = bgra[i * 4 + 2];
        uint8_t lum = static_cast<uint8_t>(0.299 * r2 + 0.587 * g + 0.114 * b);
        img.gray[i] = lum > 128 ? 255 : 0;
    }
    return img;
}

const char* Name(Detection d) {
    switch (d) {
        case Detection::Defeat: return "DEFEAT";
        case Detection::Victory: return "VICTORY";
        default: return "NONE";
    }
}

void TestOcr(const std::wstring& tesseractExe) {
    std::cout << "OCR (tesseract: " << WideToUtf8(tesseractExe) << ")\n";

    struct Case { const wchar_t* text; Detection expected; };
    Case cases[] = {
        {L"DEFEAT", Detection::Defeat},
        {L"VICTORY", Detection::Victory},
        {L"asdf qwer", Detection::None},
    };

    for (auto& c : cases) {
        std::string error;
        Detection got = RunOcr(RenderTextImage(c.text), tesseractExe, error);
        std::string detail = std::string("got ") + Name(got);
        if (!error.empty()) detail += ", error: " + error;
        Check(got == c.expected && error.empty(), "reads \"" + WideToUtf8(c.text) + "\"", detail);
    }
}

}

int main(int argc, char** argv) {
    TestLossTracker();
    TestConfig();
    TestStrings();
    TestProcessLookup();

    Config cfg;
    if (argc > 1) cfg.tesseract_path = argv[1];
    TestOcr(ResolveTesseractPath(cfg));

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
