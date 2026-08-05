#include "gui.h"
#include "screen_capture.h"
#include <windows.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    EnableDpiAwareness();
    return RunApp(hInstance, nCmdShow);
}
