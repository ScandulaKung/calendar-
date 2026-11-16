#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>

#include <string>

inline std::wstring moduleDirectory() {
  wchar_t buffer[MAX_PATH];
  const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return L"D:\\MyProjects\\calendar";
  std::wstring path(buffer, length);
  const std::wstring::size_type slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return L"D:\\MyProjects\\calendar";
  return path.substr(0, slash);
}

inline bool directoryExists(const std::wstring& path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

inline std::wstring dataDirectory() {
  static std::wstring cached;
  if (!cached.empty()) return cached;
  std::wstring dir = moduleDirectory();
  for (int hop = 0; hop < 4 && !dir.empty(); hop += 1) {
    if (directoryExists(dir + L"\\Sounds")) {
      cached = dir;
      return cached;
    }
    const std::wstring::size_type slash = dir.find_last_of(L"\\/");
    if (slash == std::wstring::npos) break;
    dir.erase(slash);
  }
  cached = moduleDirectory();
  return cached;
}

inline std::wstring dataPath(const wchar_t* relative) {
  return dataDirectory() + L"\\" + relative;
}

inline std::wstring opensslExecutable() {
  const std::wstring candidates[] = {
      dataDirectory() + L"\\openssl\\openssl.exe",
      L"D:\\Program Files\\OpenSSL-Win64\\bin\\openssl.exe",
  };
  for (const std::wstring& candidate : candidates) {
    const DWORD attributes = GetFileAttributesW(candidate.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) return candidate;
  }
  return candidates[0];
}
