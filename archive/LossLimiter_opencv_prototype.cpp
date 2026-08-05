#include "LossLimiter.h"
#include <opencv2/opencv.hpp>
#include <iostream>

DWORD FindProcessId(const char* processName)
{
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);
    
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;
    
    if (Process32First(snapshot, &entry)){
        do{
            if (strcmp(entry.szExeFile, processName) == 0){
                CloseHandle(snapshot);
                return entry.th32ProcessID;
            }
        } while (Process32Next(snapshot, &entry));
    }
    
    CloseHandle(snapshot);
    return 0;
}

bool IsProcessRunning(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == NULL){
        return false;
    }
    DWORD exitCode;
    BOOL result = GetExitCodeProcess(process, &exitCode);
    CloseHandle(process);
  
    return result && (exitCode == STILL_ACTIVE);
}

cv::Mat CaptureScreen()
{
    HDC hScreenDC = GetDC(NULL);
    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    
    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);
    
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, width, height);
    SelectObject(hMemoryDC, hBitmap);
    
    BitBlt(hMemoryDC, 0, 0, width, height, hScreenDC, 0, 0, SRCCOPY);
    
    BITMAPINFOHEADER bi = {0};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = width;
    bi.biHeight = -height;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    
    cv::Mat mat(height, width, CV_8UC4);
    GetDIBits(hMemoryDC, hBitmap, 0, height, mat.data, (BITMAPINFO*)&bi, DIB_RGB_COLORS);
    
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    ReleaseDC(NULL, hScreenDC);
    
    cv::cvtColor(mat, mat, cv::COLOR_BGRA2BGR);
    return mat;
}

// Check if template image exists in the screenshot
bool DetectTemplate(const cv::Mat& screenshot, const cv::Mat& templateImg, double threshold = 0.8)
{
    cv::Mat result;
    cv::matchTemplate(screenshot, templateImg, result, cv::TM_CCOEFF_NORMED);
    
    double minVal, maxVal;
    cv::Point minLoc, maxLoc;
    cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);
    
    return maxVal >= threshold;
}

enum GameState {
    STATE_IN_GAME,
    STATE_DEFEAT_SCREEN,
    STATE_RANK_SCREEN
};

int main()
{
    cv::Mat defeatTemplate = cv::imread("defeat_template.png");
    cv::Mat rankTemplate = cv::imread("rank_template.png");
    cv::Mat gameplayTemplate = cv::imread("gameplay_template.png");
    
    if (defeatTemplate.empty() || rankTemplate.empty() || gameplayTemplate.empty()) {
        std::cerr << "Error: Could not load template images!" << std::endl;
        return 1;
    }
    
    int consecutiveLosses = 0;
    GameState currentState = STATE_IN_GAME;
    bool defeatDetected = false;
    
    while (true)
    {
        DWORD pid = FindProcessId("Overwatch.exe");
        
        if (pid == 0 || !IsProcessRunning(pid)) {
            std::cout << "Overwatch is not running. Waiting..." << std::endl;
            Sleep(5000);
            continue;
        }
        
        cv::Mat screenshot = CaptureScreen();
        
        bool isDefeat = DetectTemplate(screenshot, defeatTemplate, 0.7);
        bool isRankScreen = DetectTemplate(screenshot, rankTemplate, 0.7);
        bool isGameplay = DetectTemplate(screenshot, gameplayTemplate, 0.7);
        
        if (isDefeat && currentState == STATE_IN_GAME) {
            currentState = STATE_DEFEAT_SCREEN;
            defeatDetected = true;
            std::cout << "DEFEAT detected!" << std::endl;
        }
        else if (isRankScreen && currentState == STATE_DEFEAT_SCREEN && defeatDetected) {
            currentState = STATE_RANK_SCREEN;
            consecutiveLosses++;
            std::cout << "Loss counted! Total: " << consecutiveLosses << "/3" << std::endl;
            
            if (consecutiveLosses >= 3) {
                std::cout << "3 losses in a row! Closing Overwatch..." << std::endl;
                HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
                if (process != NULL) {
                    TerminateProcess(process, 0);
                    CloseHandle(process);
                }
                break;
            }
        }
        else if (isGameplay && currentState == STATE_RANK_SCREEN) {
            currentState = STATE_IN_GAME;
            defeatDetected = false;
            std::cout << "Back in game. Ready to detect next loss." << std::endl;
        }
        
        Sleep(2000);
    }
    
    return 0;
}