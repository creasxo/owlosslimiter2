#include "config.h"
#include "strutil.h"
#include <windows.h>
#include <cstdio>
#include <string>

Config LoadConfig(const std::wstring& path) {
    Config cfg;
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return cfg;

    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        std::string s = Trim(line);
        if (s.empty() || s[0] == '#') continue;
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(s.substr(0, eq));
        std::string val = Trim(s.substr(eq + 1));

        if (key == "losses") {
            try { cfg.losses = std::stoi(val); } catch (...) {}
        } else if (key == "poll") {
            try { cfg.poll = std::stod(val); } catch (...) {}
        } else if (key == "process") {
            cfg.process = val;
        } else if (key == "tesseract_path") {
            cfg.tesseract_path = val;
        } else if (key == "debug") {
            cfg.debug = (val == "1" || EqualsIgnoreCase(val, "true"));
        }
    }
    fclose(f);
    return cfg;
}

void SaveConfig(const std::wstring& path, const Config& cfg) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return;
    fprintf(f, "losses=%d\n", cfg.losses);
    fprintf(f, "poll=%.3f\n", cfg.poll);
    fprintf(f, "process=%s\n", cfg.process.c_str());
    fprintf(f, "tesseract_path=%s\n", cfg.tesseract_path.c_str());
    fprintf(f, "debug=%d\n", cfg.debug ? 1 : 0);
    fclose(f);
}

std::wstring ResolveTesseractPath(const Config& cfg) {
    if (!cfg.tesseract_path.empty()) return Utf8ToWide(cfg.tesseract_path);

    const wchar_t* candidate = L"C:\\Program Files\\Tesseract-OCR\\tesseract.exe";
    DWORD attrs = GetFileAttributesW(candidate);
    if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return candidate;
    }
    return L"tesseract";
}
