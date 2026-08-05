#ifndef LOSSLIMITER_H
#define LOSSLIMITER_H

#include <windows.h>
#include <tlhelp32.h>
#include <opencv2/opencv.hpp>

// Process detection functions
DWORD FindProcessId(const char* processName);
bool IsProcessRunning(DWORD pid);

// Screenshot and detection functions
cv::Mat CaptureScreen();
bool DetectTemplate(const cv::Mat& screenshot, const cv::Mat& templateImg, double threshold);

// Game state enum
enum GameState {
    STATE_IN_GAME,
    STATE_DEFEAT_SCREEN,
    STATE_RANK_SCREEN
};

#endif