#pragma once
#include <string>

struct Config {
    int         losses         = 3;
    double      poll           = 2.0;
    std::string process        = "Overwatch.exe";
    std::string tesseract_path = "";
    bool        debug          = false;
};

Config LoadConfig(const std::wstring& path);
void   SaveConfig(const std::wstring& path, const Config& cfg);

std::wstring ResolveTesseractPath(const Config& cfg);
