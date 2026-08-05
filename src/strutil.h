#pragma once
#include <string>

std::wstring Utf8ToWide(const std::string& s);
std::string  WideToUtf8(const std::wstring& s);

std::string  Trim(const std::string& s);
bool         EqualsIgnoreCase(const std::string& a, const std::string& b);
bool         ContainsIgnoreCase(const std::string& haystack, const std::string& needle);
