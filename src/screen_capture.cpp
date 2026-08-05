#include "screen_capture.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"
#include <windows.h>

void EnableDpiAwareness() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

bool CaptureRoi(CapturedImage& out, std::string& error) {
    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) { error = "GetDC failed"; return false; }

    int width  = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);

    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, width, height);
    HGDIOBJ oldObj = SelectObject(hdcMem, hBitmap);

    BOOL blitOk = BitBlt(hdcMem, 0, 0, width, height, hdcScreen, 0, 0, SRCCOPY | CAPTUREBLT);

    BITMAPINFOHEADER bi{};
    bi.biSize        = sizeof(BITMAPINFOHEADER);
    bi.biWidth        = width;
    bi.biHeight       = -height;
    bi.biPlanes       = 1;
    bi.biBitCount     = 32;
    bi.biCompression  = BI_RGB;

    std::vector<uint8_t> bgra(static_cast<size_t>(width) * height * 4);
    int gotLines = 0;
    if (blitOk) {
        gotLines = GetDIBits(hdcMem, hBitmap, 0, height, bgra.data(),
                              reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);
    }

    SelectObject(hdcMem, oldObj);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    if (!blitOk || gotLines == 0) {
        error = "Screen capture failed. If Overwatch is in exclusive "
                "Fullscreen mode, switch to Windowed Fullscreen.";
        return false;
    }

    int x0 = static_cast<int>(width  * ROI_X_START);
    int x1 = static_cast<int>(width  * ROI_X_END);
    int y0 = static_cast<int>(height * ROI_Y_START);
    int y1 = static_cast<int>(height * ROI_Y_END);
    int cropW = x1 - x0;
    int cropH = y1 - y0;

    out.width  = cropW;
    out.height = cropH;
    out.gray.resize(static_cast<size_t>(cropW) * cropH);

    for (int y = 0; y < cropH; ++y) {
        const uint8_t* row = &bgra[(static_cast<size_t>(y0 + y) * width + x0) * 4];
        for (int x = 0; x < cropW; ++x) {
            uint8_t b = row[x * 4 + 0];
            uint8_t g = row[x * 4 + 1];
            uint8_t r = row[x * 4 + 2];
            uint8_t lum = static_cast<uint8_t>(0.299 * r + 0.587 * g + 0.114 * b);
            out.gray[static_cast<size_t>(y) * cropW + x] = lum > 128 ? 255 : 0;
        }
    }

    error.clear();
    return true;
}

std::vector<uint8_t> EncodePng(const CapturedImage& img) {
    std::vector<uint8_t> out;
    stbi_write_png_to_func(
        [](void* ctx, void* data, int size) {
            auto* vec = static_cast<std::vector<uint8_t>*>(ctx);
            const uint8_t* bytes = static_cast<const uint8_t*>(data);
            vec->insert(vec->end(), bytes, bytes + size);
        },
        &out, img.width, img.height, 1, img.gray.data(), img.width);
    return out;
}
