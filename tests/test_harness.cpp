#include "process_utils.h"
#include "screen_capture.h"
#include "ocr.h"
#include "config.h"
#include "strutil.h"
#include <windows.h>
#include <iostream>
#include <string>

namespace {

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

}

int main() {
    int failures = 0;

    std::cout << "[Test 1] FindProcessId(\"explorer.exe\")... ";
    DWORD pid = FindProcessId("explorer.exe");
    std::cout << (pid != 0 ? "PASS" : "FAIL") << " (pid=" << pid << ")\n";
    if (pid == 0) failures++;

    std::cout << "[Test 2] FindProcessId(\"NoSuchProcess12345.exe\")... ";
    DWORD missing = FindProcessId("NoSuchProcess12345.exe");
    std::cout << (missing == 0 ? "PASS" : "FAIL") << "\n";
    if (missing != 0) failures++;

    std::cout << "[Test 3] Case-insensitive match FindProcessId(\"EXPLORER.EXE\")... ";
    DWORD pidCi = FindProcessId("EXPLORER.EXE");
    std::cout << (pidCi != 0 ? "PASS" : "FAIL") << "\n";
    if (pidCi == 0) failures++;

    Config cfg;
    std::wstring tesseractExe = ResolveTesseractPath(cfg);
    std::wcout << L"Using tesseract at: " << tesseractExe << L"\n";

    struct Case { const wchar_t* text; Detection expected; };
    Case cases[] = {
        {L"DEFEAT", Detection::Defeat},
        {L"VICTORY", Detection::Victory},
        {L"asdf qwer", Detection::None},
    };

    int n = 4;
    for (auto& c : cases) {
        CapturedImage img = RenderTextImage(c.text);
        std::string error;
        Detection got = RunOcr(img, tesseractExe, error);
        bool pass = got == c.expected;
        std::wcout << L"[Test " << n++ << L"] OCR rendered \"" << c.text << L"\" -> "
                    << Name(got) << (pass ? L" PASS" : L" FAIL");
        if (!error.empty()) std::wcout << L" (error: " << Utf8ToWide(error) << L")";
        std::wcout << L"\n";
        if (!pass) failures++;
    }

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << " (" << failures << " failures)\n";
    return failures == 0 ? 0 : 1;
}
