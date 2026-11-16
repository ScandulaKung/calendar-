#pragma once

#include "paths.hpp"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>

inline void writeErrorLog(const wchar_t* kind, const std::wstring& message, bool once) {
  static std::mutex mutex;
  static std::set<std::wstring> seen;
  std::lock_guard<std::mutex> lock(mutex);
  if (once && !seen.insert(std::wstring(kind) + L"\n" + message).second) return;
  SYSTEMTIME now{};
  GetLocalTime(&now);
  wchar_t stamp[40];
  wchar_t fileName[32];
  std::swprintf(stamp, 40, L"%04u-%02u-%02u %02u:%02u:%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
  std::swprintf(fileName, 32, L"log\\%04u-%02u-%02u.log", now.wYear, now.wMonth, now.wDay);
  std::error_code error;
  std::filesystem::create_directories(std::filesystem::path(dataPath(L"log")), error);
  const std::wstring path = dataPath(fileName);
  const auto existing = std::filesystem::file_size(std::filesystem::path(path), error);
  const bool fresh = error || existing == 0;
  const std::wstring line = std::wstring(stamp) + L"  " + kind + L"  " + message + L"\r\n";
  const int size = WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
  if (size <= 0) return;
  std::string utf8(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), utf8.data(), size, nullptr, nullptr);
  std::ofstream output{std::filesystem::path(path), std::ios::binary | std::ios::app};
  if (!output) return;
  if (fresh) {
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    output.write(reinterpret_cast<const char*>(bom), 3);
  }
  output.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
}

inline void logAccessError(const std::wstring& target, const std::wstring& detail) {
  writeErrorLog(L"访问失败", detail.empty() ? target : target + L"  " + detail, false);
}

inline void logMissingResource(const std::wstring& path) {
  writeErrorLog(L"资源缺失", path, true);
}
