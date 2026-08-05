#pragma once
#include "screen_capture.h"
#include <string>
#include <optional>

enum class Detection { None, Defeat, Victory };

Detection RunOcr(const CapturedImage& img, const std::wstring& tesseractExe, std::string& error);
