#pragma once
#include <vector>
#include <cstdint>
#include <string>

constexpr double ROI_X_START = 0.30;
constexpr double ROI_X_END   = 0.70;
constexpr double ROI_Y_START = 0.20;
constexpr double ROI_Y_END   = 0.55;

struct CapturedImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> gray;
};

void EnableDpiAwareness();

bool CaptureRoi(CapturedImage& out, std::string& error);

std::vector<uint8_t> EncodePng(const CapturedImage& img);
