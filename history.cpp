#include "history.hpp"
#include "log.hpp"
#include "paths.hpp"
#include "settings.hpp"

#define UNICODE
#define _UNICODE
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
__declspec(dllimport) void* __stdcall InternetOpenW(const wchar_t*, unsigned long, const wchar_t*, const wchar_t*, unsigned long);
__declspec(dllimport) void* __stdcall InternetOpenUrlW(void*, const wchar_t*, const wchar_t*, unsigned long, unsigned long, unsigned long long);
__declspec(dllimport) int __stdcall InternetReadFile(void*, void*, unsigned long, unsigned long*);
__declspec(dllimport) int __stdcall InternetCloseHandle(void*);
__declspec(dllimport) int __stdcall InternetSetOptionW(void*, unsigned long, void*, unsigned long);
__declspec(dllimport) int __stdcall HttpQueryInfoW(void*, unsigned long, void*, unsigned long*, unsigned long*);
}

namespace {

std::mutex historyMutex;
std::mutex cacheMutex;
HistorySnapshot history;
std::atomic<bool> stopRequested{false};
std::thread worker;

void appendUtf8(std::string& out, unsigned int code) {
  if (code < 0x80) out.push_back(static_cast<char>(code));
  else if (code < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (code >> 6)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  } else if (code < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (code >> 12)));
    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (code >> 18)));
    out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  }
}

std::string httpGet(const wchar_t* url, const wchar_t* agent = L"Calendar/1.0 (desktop almanac)") {
  void* session = InternetOpenW(agent, 0, nullptr, nullptr, 0);
  if (!session) {
    logAccessError(url, L"无法打开网络 " + std::to_wstring(GetLastError()));
    return {};
  }
  unsigned long timeout = 15000;
  InternetSetOptionW(session, 2, &timeout, sizeof(timeout));
  InternetSetOptionW(session, 6, &timeout, sizeof(timeout));
  unsigned long flags = 0x80000000u | 0x04000000u | 0x00800000u;
  void* request = InternetOpenUrlW(session, url, nullptr, 0, flags, 0);
  std::string body;
  unsigned long status = 0;
  if (request) {
    InternetSetOptionW(request, 2, &timeout, sizeof(timeout));
    InternetSetOptionW(request, 6, &timeout, sizeof(timeout));
    unsigned long size = sizeof(status);
    unsigned long index = 0;
    HttpQueryInfoW(request, 19u | 0x20000000u, &status, &size, &index);
    char buffer[8192];
    unsigned long read = 0;
    while (InternetReadFile(request, buffer, sizeof(buffer), &read) && read > 0) body.append(buffer, buffer + read);
    InternetCloseHandle(request);
  } else {
    logAccessError(url, L"无法连接 " + std::to_wstring(GetLastError()));
  }
  InternetCloseHandle(session);
  if (request && (status >= 400 || (status > 0 && status < 200))) logAccessError(url, L"HTTP " + std::to_wstring(status));
  else if (request && body.empty()) logAccessError(url, L"响应为空");
  return body;
}

std::wstring utf8ToWide(const std::string& text) {
  if (text.empty()) return L"";
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (size <= 0) return L"";
  std::wstring wide(size, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
  return wide;
}

std::wstring cleanText(std::wstring text) {
  const std::wstring marks[] = {L"&amp;", L"&lt;", L"&gt;", L"&quot;"};
  const wchar_t* replacements[] = {L"&", L"<", L">", L"\""};
  for (int index = 0; index < 4; index += 1) {
    std::wstring::size_type pos = 0;
    while ((pos = text.find(marks[index], pos)) != std::wstring::npos) {
      text.replace(pos, marks[index].size(), replacements[index]);
    }
  }
  return text;
}

std::string readJsonString(const std::string& json, std::string::size_type& pos) {
  if (pos >= json.size() || json[pos] != '"') return {};
  pos += 1;
  std::string value;
  while (pos < json.size()) {
    const char c = json[pos++];
    if (c == '\\' && pos < json.size()) {
      const char escaped = json[pos++];
      if (escaped == 'u' && pos + 4 <= json.size()) {
        unsigned int code = 0;
        bool ok = true;
        for (int nibble = 0; nibble < 4; nibble += 1) {
          const char hex = json[pos++];
          code <<= 4;
          if (hex >= '0' && hex <= '9') code += static_cast<unsigned int>(hex - '0');
          else if (hex >= 'a' && hex <= 'f') code += static_cast<unsigned int>(hex - 'a' + 10);
          else if (hex >= 'A' && hex <= 'F') code += static_cast<unsigned int>(hex - 'A' + 10);
          else ok = false;
        }
        if (ok) appendUtf8(value, code);
      } else if (escaped == 'n') value.push_back('\n');
      else if (escaped == 'r') value.push_back('\r');
      else if (escaped == 't') value.push_back('\t');
      else value.push_back(escaped);
      continue;
    }
    if (c == '"') break;
    value.push_back(c);
  }
  return value;
}

void skipSpace(const std::string& json, std::string::size_type& pos) {
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) pos += 1;
}

struct StoredEvent {
  int year = 0;
  std::wstring text;
};

void readEvents(const std::string& json, std::vector<StoredEvent>& events) {
  const std::string::size_type eventsKey = json.find("\"events\"");
  std::string::size_type pos = eventsKey == std::string::npos ? 0 : eventsKey;
  if (eventsKey != std::string::npos) {
    pos = json.find('[', eventsKey);
    if (pos == std::string::npos) return;
  }
  int depth = 0;
  bool haveText = false;
  bool haveYear = false;
  int textDepth = -1;
  int yearDepth = -1;
  std::string pendingText;
  int pendingYear = 0;
  auto tryEmit = [&]() {
    if (!haveText || !haveYear || textDepth != yearDepth) return;
    StoredEvent event;
    event.year = pendingYear;
    event.text = cleanText(utf8ToWide(pendingText));
    if (!event.text.empty()) events.push_back(event);
    haveText = false;
    haveYear = false;
  };
  while (pos < json.size()) {
    const char c = json[pos];
    if (c == '"') {
      const std::string token = readJsonString(json, pos);
      const std::string::size_type after = pos;
      skipSpace(json, pos);
      if (pos >= json.size() || json[pos] != ':') {
        pos = after;
        continue;
      }
      pos += 1;
      skipSpace(json, pos);
      if (token == "text" && pos < json.size() && json[pos] == '"' && !haveText) {
        const std::string value = readJsonString(json, pos);
        if (!value.empty()) {
          pendingText = value;
          textDepth = depth;
          haveText = true;
          tryEmit();
        }
      } else if (token == "year" && pos < json.size() && (json[pos] == '-' || (json[pos] >= '0' && json[pos] <= '9'))) {
        pendingYear = std::atoi(json.c_str() + pos);
        yearDepth = depth;
        haveYear = true;
        tryEmit();
        while (pos < json.size() && (json[pos] == '-' || (json[pos] >= '0' && json[pos] <= '9'))) pos += 1;
      }
      continue;
    }
    if (c == '{' || c == '[') {
      depth += 1;
      pos += 1;
      if (eventsKey != std::string::npos && depth == 0) break;
      continue;
    }
    if (c == '}' || c == ']') {
      depth -= 1;
      if (haveText && textDepth > depth) haveText = false;
      if (haveYear && yearDepth > depth) haveYear = false;
      pos += 1;
      if (eventsKey != std::string::npos && depth == 0) break;
      continue;
    }
    pos += 1;
  }
}

std::wstring historyCachePath() {
  return dataPath(L"history_cache.txt");
}

std::string wideToUtf8(const std::wstring& text) {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string utf8(size, '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr);
  return utf8;
}

std::string dateHeader(int month, int day) {
  char header[16];
  std::snprintf(header, sizeof(header), "#%02d-%02d", month, day);
  return header;
}

std::string oneLine(const std::wstring& text) {
  std::wstring flat = text;
  for (wchar_t& ch : flat) {
    if (ch == L'\n' || ch == L'\r' || ch == L'\t') ch = L' ';
  }
  return wideToUtf8(flat);
}

int daysInMonth(int month) {
  static const int days[] = {0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  return days[month];
}

std::string endMarker(int month, int day) {
  return dateHeader(month, day) + " end";
}

std::string wikiMarker(int month, int day) {
  return dateHeader(month, day) + " wiki";
}

bool sectionComplete(int month, int day) {
  std::ifstream input{std::filesystem::path(historyCachePath())};
  if (!input) return false;
  const std::string marker = endMarker(month, day);
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line == marker) return true;
  }
  return false;
}

void publishHistory(int month, int day, const std::vector<StoredEvent>& events) {
  HistorySnapshot next;
  next.month = month;
  next.day = day;
  for (const StoredEvent& event : events) {
    if (next.count >= 3) break;
    next.years[next.count] = event.year;
    next.texts[next.count] = event.text;
    next.count += 1;
  }
  next.ready = next.count > 0;
  std::lock_guard<std::mutex> lock(historyMutex);
  history = next;
}

bool loadLocalHistory(int month, int day, HistorySnapshot& snapshot) {
  std::ifstream input{std::filesystem::path(historyCachePath())};
  if (!input) return false;
  const std::string header = dateHeader(month, day);
  bool inSection = false;
  bool sawHeader = false;
  std::string line;
  snapshot = HistorySnapshot{};
  snapshot.month = month;
  snapshot.day = day;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty() && line[0] == '#') {
      if (inSection) break;
      inSection = line == header;
      if (inSection) sawHeader = true;
      continue;
    }
    if (!inSection || line.empty() || snapshot.count >= 3) continue;
    const std::string::size_type split = line.find(' ');
    if (split == std::string::npos || split == 0) continue;
    snapshot.years[snapshot.count] = std::atoi(line.c_str());
    snapshot.texts[snapshot.count] = utf8ToWide(line.substr(split + 1));
    if (!snapshot.texts[snapshot.count].empty()) snapshot.count += 1;
  }
  if (!sawHeader || snapshot.count <= 0) return false;
  snapshot.ready = true;
  return true;
}

void saveLocalHistory(int month, int day, const std::vector<StoredEvent>& events) {
  const std::string header = dateHeader(month, day);
  const std::string marker = wikiMarker(month, day);
  const std::string ended = endMarker(month, day);
  std::vector<std::string> kept;
  {
    std::ifstream input{std::filesystem::path(historyCachePath())};
    if (input) {
      bool skipping = false;
      std::string line;
      while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] == '#') skipping = line == header || line == marker || line == ended;
        if (!skipping) kept.push_back(line);
      }
    }
  }
  std::ofstream output{std::filesystem::path(historyCachePath()), std::ios::binary};
  if (!output) return;
  for (const std::string& line : kept) output << line << '\n';
  output << header << '\n';
  for (const StoredEvent& event : events) output << event.year << ' ' << oneLine(event.text) << '\n';
  output << marker << '\n';
}

std::string stripTags(const std::string& html) {
  std::string text;
  bool inTag = false;
  for (char c : html) {
    if (c == '<') {
      inTag = true;
      continue;
    }
    if (c == '>') {
      inTag = false;
      continue;
    }
    if (!inTag) text.push_back(c);
  }
  return text;
}

std::string::size_type matchingBracket(const std::string& json, std::string::size_type open) {
  int depth = 0;
  bool inString = false;
  bool escape = false;
  for (std::string::size_type index = open; index < json.size(); index += 1) {
    const char c = json[index];
    if (inString) {
      if (escape) escape = false;
      else if (c == '\\') escape = true;
      else if (c == '"') inString = false;
      continue;
    }
    if (c == '"') {
      inString = true;
      continue;
    }
    if (c == '[' || c == '{') depth += 1;
    else if (c == ']' || c == '}') {
      depth -= 1;
      if (depth == 0) return index;
    }
  }
  return std::string::npos;
}

bool readBaiduEvent(const std::string& slice, StoredEvent& event) {
  const std::string::size_type titleKey = slice.find("\"title\"");
  const std::string::size_type yearKey = slice.find("\"year\"");
  if (titleKey == std::string::npos || yearKey == std::string::npos) return false;
  const std::string::size_type titleColon = slice.find(':', titleKey);
  const std::string::size_type titleQuote = titleColon == std::string::npos ? std::string::npos : slice.find('"', titleColon + 1);
  if (titleQuote == std::string::npos) return false;
  std::string::size_type cursor = titleQuote;
  const std::wstring title = cleanText(utf8ToWide(stripTags(readJsonString(slice, cursor))));
  const std::string::size_type yearColon = slice.find(':', yearKey);
  if (yearColon == std::string::npos) return false;
  std::string::size_type yearPos = yearColon + 1;
  while (yearPos < slice.size() && (slice[yearPos] == ' ' || slice[yearPos] == '"')) yearPos += 1;
  event.year = std::atoi(slice.c_str() + yearPos);
  event.text = title;
  return event.year != 0 && !event.text.empty();
}

bool parseBaiduDay(const std::string& json, int month, int day, std::vector<StoredEvent>& events) {
  char key[16];
  std::snprintf(key, sizeof(key), "\"%02d%02d\":", month, day);
  const std::string::size_type found = json.find(key);
  if (found == std::string::npos) return false;
  const std::string::size_type open = json.find('[', found);
  if (open == std::string::npos) return false;
  const std::string::size_type close = matchingBracket(json, open);
  if (close == std::string::npos) return false;
  std::string::size_type pos = open + 1;
  while (pos < close) {
    const std::string::size_type obj = json.find('{', pos);
    if (obj == std::string::npos || obj >= close) break;
    const std::string::size_type objEnd = matchingBracket(json, obj);
    if (objEnd == std::string::npos || objEnd > close) break;
    StoredEvent event;
    if (readBaiduEvent(json.substr(obj, objEnd - obj + 1), event)) events.push_back(event);
    pos = objEnd + 1;
  }
  std::stable_sort(events.begin(), events.end(), [](const StoredEvent& left, const StoredEvent& right) { return left.year > right.year; });
  return true;
}

bool monthReady(int month) {
  const int lastDay = daysInMonth(month);
  bool seen[32] = {};
  std::ifstream input{std::filesystem::path(historyCachePath())};
  if (!input) return false;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    for (int day = 1; day <= lastDay; day += 1) {
      if (line == endMarker(month, day)) seen[day] = true;
    }
  }
  for (int day = 1; day <= lastDay; day += 1) {
    if (!seen[day]) return false;
  }
  return true;
}

void saveMonth(int month, const std::vector<std::vector<StoredEvent>>& days, const std::vector<char>& present) {
  char prefix[8];
  std::snprintf(prefix, sizeof(prefix), "#%02d-", month);
  std::vector<std::string> kept;
  {
    std::ifstream input{std::filesystem::path(historyCachePath())};
    if (input) {
      bool skipping = false;
      std::string line;
      while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] == '#') skipping = line.rfind(prefix, 0) == 0;
        if (!skipping) kept.push_back(line);
      }
    }
  }
  std::ofstream output{std::filesystem::path(historyCachePath()), std::ios::binary};
  if (!output) return;
  for (const std::string& line : kept) output << line << '\n';
  const int lastDay = daysInMonth(month);
  for (int day = 1; day <= lastDay; day += 1) {
    if (!present[day]) continue;
    output << dateHeader(month, day) << '\n';
    for (const StoredEvent& event : days[day]) output << event.year << ' ' << oneLine(event.text) << '\n';
    output << endMarker(month, day) << '\n';
  }
}

bool storeBaiduMonth(int month) {
  wchar_t url[200];
  std::swprintf(url, 200, L"https://baike.baidu.com/cms/home/eventsOnHistory/%02d.json", month);
  const std::string body = httpGet(url, L"Mozilla/5.0");
  if (body.find("\"year\"") == std::string::npos) return false;
  const int lastDay = daysInMonth(month);
  std::vector<std::vector<StoredEvent>> days(lastDay + 1);
  std::vector<char> present(lastDay + 1, 0);
  int found = 0;
  for (int day = 1; day <= lastDay; day += 1) {
    if (!parseBaiduDay(body, month, day, days[day])) continue;
    present[day] = 1;
    found += 1;
  }
  if (found * 2 < lastDay) return false;
  for (int day = 1; day <= lastDay; day += 1) present[day] = 1;
  saveMonth(month, days, present);
  return monthReady(month);
}

void fetchHistory(int month, int day) {
  HistorySnapshot cached;
  if (loadLocalHistory(month, day, cached)) {
    std::lock_guard<std::mutex> lock(historyMutex);
    history = cached;
    return;
  }
  if (!monthReady(month)) storeBaiduMonth(month);
  if (loadLocalHistory(month, day, cached)) {
    std::lock_guard<std::mutex> lock(historyMutex);
    history = cached;
  }
}

std::wstring wikiJsonPath(int month, int day) {
  wchar_t name[32];
  std::swprintf(name, 32, L"%02d-%02d.json", month, day);
  return dataPath(L"wiki_pages") + L"\\" + name;
}

bool readWholeFile(const std::wstring& path, std::string& body) {
  std::ifstream input{std::filesystem::path(path), std::ios::binary};
  if (!input) return false;
  body.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  return !body.empty();
}

bool runHidden(const std::wstring& command) {
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION process{};
  std::wstring line = command;
  if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return false;
  WaitForSingleObject(process.hProcess, 40000);
  DWORD code = 1;
  GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hProcess);
  CloseHandle(process.hThread);
  return code == 0;
}

std::wstring systemProxy() {
  DWORD enabled = 0;
  DWORD size = sizeof(enabled);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings", L"ProxyEnable", RRF_RT_REG_DWORD, nullptr, &enabled, &size) != ERROR_SUCCESS || !enabled) return {};
  wchar_t server[512] = {};
  DWORD bytes = sizeof(server);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings", L"ProxyServer", RRF_RT_REG_SZ, nullptr, server, &bytes) != ERROR_SUCCESS) return {};
  std::wstring proxy = server;
  const std::wstring::size_type https = proxy.find(L"https=");
  if (https != std::wstring::npos) {
    proxy = proxy.substr(https + 6);
    const std::wstring::size_type end = proxy.find(L';');
    if (end != std::wstring::npos) proxy = proxy.substr(0, end);
  }
  if (proxy.rfind(L"http://", 0) != 0 && proxy.rfind(L"https://", 0) != 0) proxy = L"http://" + proxy;
  return proxy;
}

bool downloadWikipedia(const std::wstring& url, const std::wstring& path) {
  wchar_t temp[MAX_PATH] = {};
  if (GetTempFileNameW(dataPath(L"wiki_pages").c_str(), L"dl", 0, temp) == 0) return false;
  const std::wstring proxy = systemProxy();
  std::wstring command = L"curl.exe -f -sS --max-time 25 -A \"Calendar/1.0 (desktop almanac)\"";
  if (!proxy.empty()) command += L" -x \"" + proxy + L"\"";
  command += L" -o \"" + std::wstring(temp) + L"\" \"" + url + L"\"";
  const bool ok = runHidden(command);
  std::error_code error;
  if (ok) std::filesystem::rename(std::filesystem::path(temp), std::filesystem::path(path), error);
  if (!ok || error) {
    std::filesystem::remove(std::filesystem::path(temp), error);
    logAccessError(url, L"下载失败");
    return false;
  }
  return true;
}

bool loadWikipedia(int month, int day, std::vector<StoredEvent>& events) {
  const std::wstring path = wikiJsonPath(month, day);
  std::string body;
  if (!readWholeFile(path, body) || body.find("\"events\"") == std::string::npos) {
    wchar_t url[180];
    std::swprintf(url, 180, L"https://zh.wikipedia.org/api/rest_v1/feed/onthisday/events/%d/%d", month, day);
    std::error_code error;
    std::filesystem::create_directories(dataPath(L"wiki_pages"), error);
    if (!downloadWikipedia(url, path) || !readWholeFile(path, body) || body.find("\"events\"") == std::string::npos) return false;
  }
  readEvents(body, events);
  return !events.empty();
}

void prefetchHistory() {
  struct Day {
    int month;
    int day;
  };
  std::vector<Day> pending;
  bool saved[13][32] = {};
  {
    std::ifstream input{std::filesystem::path(historyCachePath())};
    std::string line;
    while (std::getline(input, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.size() < 11 || line.compare(line.size() - 5, 5, " wiki") != 0) continue;
      const int month = std::atoi(line.c_str() + 1);
      const int day = std::atoi(line.c_str() + 4);
      if (month >= 1 && month <= 12 && day >= 1 && day <= 31) saved[month][day] = true;
    }
  }
  SYSTEMTIME today{};
  GetLocalTime(&today);
  for (int month = 1; month <= 12; month += 1) {
    for (int day = 1; day <= daysInMonth(month); day += 1) {
      if (!saved[month][day]) pending.push_back(Day{month, day});
    }
  }
  if (!pending.empty() && (pending.front().month != today.wMonth || pending.front().day != today.wDay)) {
    for (size_t index = 0; index < pending.size(); index += 1) {
      if (pending[index].month == today.wMonth && pending[index].day == today.wDay) {
        std::swap(pending[index], pending.front());
        break;
      }
    }
  }
  for (const Day& day : pending) {
    if (stopRequested.load()) break;
    SYSTEMTIME clock{};
    GetLocalTime(&clock);
    if (clock.wYear != today.wYear || clock.wMonth != today.wMonth || clock.wDay != today.wDay) break;
    if (calendarSettings().language != 0) break;
    std::vector<StoredEvent> events;
    bool savedDay = false;
    for (int attempt = 0; attempt < 4 && !stopRequested.load(); attempt += 1) {
      events.clear();
      if (loadWikipedia(day.month, day.day, events)) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        saveLocalHistory(day.month, day.day, events);
        savedDay = true;
        break;
      }
      std::error_code error;
      std::filesystem::remove(std::filesystem::path(wikiJsonPath(day.month, day.day)), error);
      Sleep(5000);
    }
    SYSTEMTIME now{};
    GetLocalTime(&now);
    if (savedDay && now.wMonth == day.month && now.wDay == day.day) publishHistory(day.month, day.day, events);
    Sleep(1100);
  }
}

bool loadEnglishDay(int month, int day, std::vector<StoredEvent>& events) {
  wchar_t name[40];
  std::swprintf(name, 40, L"en\\%02d-%02d.json", month, day);
  const std::wstring path = dataPath(L"wiki_pages\\") + name;
  std::string body;
  if (!readWholeFile(path, body) || body.find("\"events\"") == std::string::npos) {
    wchar_t url[200];
    std::swprintf(url, 200, L"https://en.wikipedia.org/api/rest_v1/feed/onthisday/events/%d/%d", month, day);
    std::error_code error;
    std::filesystem::create_directories(dataPath(L"wiki_pages\\en"), error);
    if (!downloadWikipedia(url, path) || !readWholeFile(path, body) || body.find("\"events\"") == std::string::npos) return false;
  }
  readEvents(body, events);
  std::stable_sort(events.begin(), events.end(), [](const StoredEvent& left, const StoredEvent& right) { return left.year > right.year; });
  return !events.empty();
}

void fetchEnglishHistory(int month, int day) {
  std::vector<StoredEvent> events;
  if (!loadEnglishDay(month, day, events)) {
    std::lock_guard<std::mutex> lock(historyMutex);
    history = HistorySnapshot{};
    history.month = month;
    history.day = day;
    return;
  }
  publishHistory(month, day, events);
}

void historyLoop() {
  int month = 0;
  int day = 0;
  int language = -1;
  while (!stopRequested.load()) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    const int currentLanguage = calendarSettings().language;
    if (now.wMonth != month || now.wDay != day || currentLanguage != language) {
      month = now.wMonth;
      day = now.wDay;
      language = currentLanguage;
      if (language == 1) fetchEnglishHistory(month, day);
      else fetchHistory(month, day);
    }
    if (language == 0) prefetchHistory();
    while (!stopRequested.load()) {
      SYSTEMTIME later{};
      GetLocalTime(&later);
      if (later.wMonth != month || later.wDay != day || calendarSettings().language != language) break;
      Sleep(1000);
    }
  }
}

}

void startHistoryUpdates() {
  if (worker.joinable()) return;
  stopRequested.store(false);
  worker = std::thread(historyLoop);
}

void stopHistoryUpdates() {
  stopRequested.store(true);
  if (worker.joinable()) worker.join();
}

HistorySnapshot currentHistory() {
  std::lock_guard<std::mutex> lock(historyMutex);
  return history;
}

std::vector<HistoryEntry> historyForDate(int month, int day) {
  std::vector<HistoryEntry> lines;
  if (month < 1 || month > 12 || day < 1 || day > 31) return lines;
  std::ifstream input{std::filesystem::path(historyCachePath())};
  if (!input) return lines;
  const std::string header = dateHeader(month, day);
  bool inSection = false;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty() && line[0] == '#') {
      if (inSection) break;
      inSection = line == header;
      continue;
    }
    if (!inSection || line.empty() || lines.size() >= 8) continue;
    const std::string::size_type split = line.find(' ');
    if (split == std::string::npos || split == 0) continue;
    HistoryEntry entry;
    entry.year = std::atoi(line.c_str());
    entry.text = utf8ToWide(line.substr(split + 1));
    if (!entry.text.empty()) lines.push_back(entry);
  }
  return lines;
}
