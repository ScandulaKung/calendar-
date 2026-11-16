#define UNICODE
#define _UNICODE
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "china_cities.hpp"
#include "log.hpp"
#include "paths.hpp"
#include "date_calc.hpp"
#include "weather.hpp"

#include "settings.hpp"

#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <ctime>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>

extern "C" {
__declspec(dllimport) void* __stdcall InternetOpenW(const wchar_t*, unsigned long, const wchar_t*, const wchar_t*, unsigned long);
__declspec(dllimport) void* __stdcall InternetOpenUrlW(void*, const wchar_t*, const wchar_t*, unsigned long, unsigned long, unsigned long long);
__declspec(dllimport) int __stdcall InternetReadFile(void*, void*, unsigned long, unsigned long*);
__declspec(dllimport) int __stdcall InternetCloseHandle(void*);
__declspec(dllimport) int __stdcall InternetSetOptionW(void*, unsigned long, void*, unsigned long);
__declspec(dllimport) int __stdcall HttpQueryInfoW(void*, unsigned long, void*, unsigned long*, unsigned long*);
}

namespace {

std::mutex weatherMutex;
WeatherSnapshot weather;
std::atomic<bool> stopRequested{false};
std::atomic<bool> refreshRequested{false};
std::thread worker;

std::string inflateGzip(const std::string& compressed) {
  z_stream stream{};
  if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) return {};
  stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
  stream.avail_in = static_cast<unsigned int>(compressed.size());
  std::string plain;
  char buffer[4096];
  int result = Z_OK;
  while (result == Z_OK) {
    stream.next_out = reinterpret_cast<Bytef*>(buffer);
    stream.avail_out = sizeof(buffer);
    result = inflate(&stream, Z_NO_FLUSH);
    if (result != Z_OK && result != Z_STREAM_END) break;
    plain.append(buffer, sizeof(buffer) - stream.avail_out);
  }
  inflateEnd(&stream);
  return result == Z_STREAM_END ? plain : std::string{};
}

std::string httpGet(const wchar_t* url, const wchar_t* headers = nullptr, bool direct = false) {
  void* session = InternetOpenW(L"calendar", direct ? 1u : 0u, nullptr, nullptr, 0);
  if (!session) {
    logAccessError(url, L"无法打开网络 " + std::to_wstring(GetLastError()));
    return {};
  }
  unsigned long timeout = 8000;
  InternetSetOptionW(session, 2, &timeout, sizeof(timeout));
  InternetSetOptionW(session, 6, &timeout, sizeof(timeout));
  const bool secure = std::wcsncmp(url, L"https://", 8) == 0;
  unsigned long flags = 0x80000000u | 0x04000000u;
  if (secure) flags |= 0x00800000u;
  void* request = InternetOpenUrlW(session, url, headers, headers ? static_cast<unsigned long>(-1) : 0, flags, 0);
  std::string body;
  unsigned long status = 0;
  if (request) {
    unsigned long size = sizeof(status);
    unsigned long index = 0;
    HttpQueryInfoW(request, 19u | 0x20000000u, &status, &size, &index);
    char buffer[4096];
    unsigned long read = 0;
    while (InternetReadFile(request, buffer, sizeof(buffer), &read) && read > 0) body.append(buffer, buffer + read);
    InternetCloseHandle(request);
  } else {
    logAccessError(url, L"无法连接 " + std::to_wstring(GetLastError()));
  }
  InternetCloseHandle(session);
  if (request && (status >= 400 || (status > 0 && status < 200))) logAccessError(url, L"HTTP " + std::to_wstring(status));
  else if (request && body.empty()) logAccessError(url, L"响应为空");
  if (body.size() >= 2 && static_cast<unsigned char>(body[0]) == 0x1f && static_cast<unsigned char>(body[1]) == 0x8b) {
    const std::string plain = inflateGzip(body);
    if (!plain.empty()) return plain;
    logAccessError(url, L"响应无法解压");
  }
  return body;
}

bool numberAfter(const std::string& json, const char* key, double& value) {
  const std::string token = std::string("\"") + key + "\":";
  std::string::size_type pos = 0;
  while ((pos = json.find(token, pos)) != std::string::npos) {
    std::string::size_type valuePos = pos + token.size();
    while (valuePos < json.size() && (json[valuePos] == ' ' || json[valuePos] == '\n')) valuePos += 1;
    if (valuePos < json.size() && (json[valuePos] == '-' || (json[valuePos] >= '0' && json[valuePos] <= '9'))) {
      char* end = nullptr;
      value = std::strtod(json.c_str() + valuePos, &end);
      return end != json.c_str() + valuePos;
    }
    pos = valuePos;
  }
  return false;
}

std::wstring textAfter(const std::string& json, const char* key) {
  const std::string pattern = std::string("\"") + key + "\":\"";
  const std::string::size_type start = json.find(pattern);
  if (start == std::string::npos) return L"";
  const std::string::size_type from = start + pattern.size();
  const std::string::size_type end = json.find('"', from);
  if (end == std::string::npos) return L"";
  const std::string utf8 = json.substr(from, end - from);
  if (utf8.empty()) return L"";
  const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring wide(size, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), size);
  return wide;
}

bool coordinateUsable(double latitude, double longitude) {
  return latitude >= -90.0 && latitude <= 90.0 && longitude >= -180.0 && longitude <= 180.0 && (std::fabs(latitude) > 0.01 || std::fabs(longitude) > 0.01);
}

bool quotedNumber(const std::string& json, const char* key, double& value) {
  const std::string quoted = std::string("\"") + key + "\":\"";
  const std::string::size_type start = json.find(quoted);
  if (start != std::string::npos) {
    char* end = nullptr;
    value = std::strtod(json.c_str() + start + quoted.size(), &end);
    return end != json.c_str() + start + quoted.size();
  }
  return numberAfter(json, key, value);
}

std::string gbkToUtf8(const std::string& gbk) {
  if (gbk.empty()) return {};
  const int wideCount = MultiByteToWideChar(936, 0, gbk.data(), static_cast<int>(gbk.size()), nullptr, 0);
  if (wideCount <= 0) return {};
  std::wstring wide(wideCount, L'\0');
  MultiByteToWideChar(936, 0, gbk.data(), static_cast<int>(gbk.size()), wide.data(), wideCount);
  const int utf8Count = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideCount, nullptr, 0, nullptr, nullptr);
  if (utf8Count <= 0) return {};
  std::string utf8(utf8Count, '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideCount, utf8.data(), utf8Count, nullptr, nullptr);
  return utf8;
}

void applyCityLabel(double latitude, double longitude, const std::wstring& hint, std::wstring& city) {
  const std::wstring nearest = chineseCityName(latitude, longitude, hint);
  city = nearest.empty() ? hint : nearest;
}

bool readDomesticLocation(double& latitude, double& longitude, std::wstring& city) {
  const std::string ip9 = httpGet(L"https://ip9.com.cn/get", nullptr, true);
  double lat = 0;
  double lon = 0;
  if (!ip9.empty() && (ip9.find("\"ret\":200") == std::string::npos || !quotedNumber(ip9, "lat", lat) || !quotedNumber(ip9, "lng", lon) || !coordinateUsable(lat, lon))) {
    logAccessError(L"https://ip9.com.cn/get", L"未返回坐标");
  }
  if (ip9.find("\"ret\":200") != std::string::npos && quotedNumber(ip9, "lat", lat) && quotedNumber(ip9, "lng", lon) && coordinateUsable(lat, lon)) {
    latitude = lat;
    longitude = lon;
    const std::wstring area = textAfter(ip9, "area");
    applyCityLabel(lat, lon, area, city);
    if (city.empty()) city = textAfter(ip9, "city");
    return true;
  }
  const std::string pacific = gbkToUtf8(httpGet(L"https://whois.pconline.com.cn/ipJson.jsp?json=true", nullptr, true));
  const std::wstring province = textAfter(pacific, "pro");
  const std::wstring pacificCity = textAfter(pacific, "city");
  if (!pacificCity.empty() && chinaCityCenter(province, pacificCity, lat, lon)) {
    latitude = lat;
    longitude = lon;
    applyCityLabel(lat, lon, pacificCity, city);
    return true;
  }
  if (!pacific.empty()) logAccessError(L"https://whois.pconline.com.cn/ipJson.jsp?json=true", L"未返回坐标");
  return false;
}

bool readLocation(double& latitude, double& longitude, std::wstring& city) {
  if (readDomesticLocation(latitude, longitude, city)) return true;
  const std::string whois = httpGet(L"https://ipwho.is/", nullptr, true);
  double lat = 0;
  double lon = 0;
  if (whois.find("\"success\":true") != std::string::npos && numberAfter(whois, "latitude", lat) && numberAfter(whois, "longitude", lon) && coordinateUsable(lat, lon)) {
    latitude = lat;
    longitude = lon;
    city = textAfter(whois, "city");
    return true;
  }
  if (!whois.empty()) logAccessError(L"https://ipwho.is/", L"未返回坐标");
  const std::string ipApi = httpGet(L"http://ip-api.com/json/?lang=zh-CN&fields=status,city,lat,lon", nullptr, true);
  if (ipApi.find("\"status\":\"success\"") != std::string::npos && numberAfter(ipApi, "lat", lat) && numberAfter(ipApi, "lon", lon) && coordinateUsable(lat, lon)) {
    latitude = lat;
    longitude = lon;
    city = textAfter(ipApi, "city");
    return true;
  }
  if (!ipApi.empty()) logAccessError(L"http://ip-api.com/json/", L"未返回坐标");
  logAccessError(L"定位", L"地址来源都没有返回可用坐标");
  return false;
}

int readNumberArray(const std::string& json, const char* key, double* values, int capacity) {
  const std::string token = std::string("\"") + key + "\":[";
  const std::string::size_type found = json.find(token);
  if (found == std::string::npos) return 0;
  std::string::size_type pos = found + token.size();
  int count = 0;
  while (count < capacity && pos < json.size() && json[pos] != ']') {
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ',' || json[pos] == '\n' || json[pos] == '\r')) pos += 1;
    if (pos >= json.size() || json[pos] == ']') break;
    char* end = nullptr;
    const double value = std::strtod(json.c_str() + pos, &end);
    if (end == json.c_str() + pos) break;
    values[count] = value;
    count += 1;
    pos = static_cast<std::string::size_type>(end - json.c_str());
  }
  return count;
}

int readDateArray(const std::string& json, int* years, int* months, int* days, int capacity) {
  const std::string token = "\"time\":[";
  const std::string::size_type found = json.find(token);
  if (found == std::string::npos) return 0;
  std::string::size_type pos = found + token.size();
  int count = 0;
  while (count < capacity && pos < json.size() && json[pos] != ']') {
    const std::string::size_type quote = json.find('"', pos);
    if (quote == std::string::npos || quote > json.find(']', pos)) break;
    int year = 0;
    int month = 0;
    int day = 0;
    if (std::sscanf(json.c_str() + quote + 1, "%d-%d-%d", &year, &month, &day) == 3 && isValidDate(year, month, day)) {
      years[count] = year;
      months[count] = month;
      days[count] = day;
      count += 1;
    }
    pos = quote + 1;
  }
  return count;
}

bool readForecast(double latitude, double longitude, WeatherSnapshot& snapshot) {
  wchar_t url[512];
  std::swprintf(
      url,
      512,
      L"https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,wind_direction_10m&daily=weather_code,temperature_2m_max,temperature_2m_min,wind_speed_10m_max,wind_direction_10m_dominant&timezone=auto&forecast_days=7",
      latitude,
      longitude);
  const std::string body = httpGet(url);
  double temperature = 0;
  double weatherCode = 0;
  if (!numberAfter(body, "temperature_2m", temperature) || !numberAfter(body, "weather_code", weatherCode)) {
    if (!body.empty()) logAccessError(url, L"备用天气无法解析");
    return false;
  }
  snapshot.temperature = temperature;
  snapshot.code = static_cast<int>(weatherCode);
  double humidity = 0;
  snapshot.humidity = numberAfter(body, "relative_humidity_2m", humidity) ? static_cast<int>(std::lround(humidity)) : -1;
  double windSpeed = 0;
  double windDirection = 0;
  snapshot.windSpeed = numberAfter(body, "wind_speed_10m", windSpeed) ? static_cast<int>(std::lround(windSpeed)) : 0;
  snapshot.windDirection = numberAfter(body, "wind_direction_10m", windDirection) ? static_cast<int>(std::lround(windDirection)) : 0;

  double codes[7]{};
  double highs[7]{};
  double lows[7]{};
  double speeds[7]{};
  double directions[7]{};
  int years[7]{};
  int months[7]{};
  int days[7]{};
  const int dates = readDateArray(body, years, months, days, 7);
  const int codeCount = readNumberArray(body, "weather_code", codes, 7);
  const int highCount = readNumberArray(body, "temperature_2m_max", highs, 7);
  const int lowCount = readNumberArray(body, "temperature_2m_min", lows, 7);
  const int speedCount = readNumberArray(body, "wind_speed_10m_max", speeds, 7);
  const int directionCount = readNumberArray(body, "wind_direction_10m_dominant", directions, 7);
  int count = dates;
  if (codeCount < count) count = codeCount;
  if (highCount < count) count = highCount;
  if (lowCount < count) count = lowCount;
  snapshot.dayCount = count;
  for (int index = 0; index < count; index += 1) {
    ForecastDay& day = snapshot.days[index];
    day.year = years[index];
    day.month = months[index];
    day.day = days[index];
    day.weekday = weekdayIndex(day.year, day.month, day.day);
    day.code = static_cast<int>(std::lround(codes[index]));
    day.high = static_cast<int>(std::lround(highs[index]));
    day.low = static_cast<int>(std::lround(lows[index]));
    day.windSpeed = index < speedCount ? static_cast<int>(std::lround(speeds[index])) : 0;
    day.windDirection = index < directionCount ? static_cast<int>(std::lround(directions[index])) : 0;
  }

  wchar_t airUrl[256];
  std::swprintf(airUrl, 256, L"https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f&longitude=%.4f&current=us_aqi", latitude, longitude);
  const std::string air = httpGet(airUrl);
  double aqi = 0;
  snapshot.aqi = numberAfter(air, "us_aqi", aqi) ? static_cast<int>(std::lround(aqi)) : -1;
  return true;
}

std::wstring quotaPath() {
  return dataPath(L"qweather_quota.txt");
}

int quotaMonthKey() {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  return now.wYear * 100 + now.wMonth;
}

int quotaUsed() {
  const int monthKey = quotaMonthKey();
  std::ifstream input{std::filesystem::path(quotaPath())};
  int storedMonth = 0;
  int count = 0;
  std::string line;
  while (std::getline(input, line)) {
    if (line.rfind("month=", 0) == 0) storedMonth = std::atoi(line.c_str() + 6);
    if (line.rfind("count=", 0) == 0) count = std::atoi(line.c_str() + 6);
  }
  return storedMonth == monthKey ? count : 0;
}

void quotaAdd(int amount) {
  const int monthKey = quotaMonthKey();
  const int count = quotaUsed() + amount;
  std::ofstream output{std::filesystem::path(quotaPath()), std::ios::binary};
  if (!output) return;
  output << "month=" << monthKey << "\ncount=" << count << "\n";
}

std::wstring trimCopy(std::wstring text) {
  while (!text.empty() && (text.front() == L' ' || text.front() == L'\t')) text.erase(text.begin());
  while (!text.empty() && (text.back() == L' ' || text.back() == L'\t' || text.back() == L'/')) text.pop_back();
  if (text.rfind(L"https://", 0) == 0) text = text.substr(8);
  if (text.rfind(L"http://", 0) == 0) text = text.substr(7);
  return text;
}

bool numberNear(const std::string& json, std::string::size_type from, const char* key, double& value) {
  const std::string token = std::string("\"") + key + "\"";
  const std::string::size_type found = json.find(token, from);
  if (found == std::string::npos) return false;
  const std::string::size_type colon = json.find(':', found + token.size());
  if (colon == std::string::npos) return false;
  value = std::strtod(json.c_str() + colon + 1, nullptr);
  return true;
}

bool textNear(const std::string& json, std::string::size_type from, const char* key, std::string& value) {
  const std::string token = std::string("\"") + key + "\":\"";
  const std::string::size_type found = json.find(token, from);
  if (found == std::string::npos) return false;
  const std::string::size_type start = found + token.size();
  const std::string::size_type end = json.find('"', start);
  if (end == std::string::npos) return false;
  value = json.substr(start, end - start);
  return true;
}

bool objectNumber(const std::string& json, std::string::size_type from, const char* key, double& value) {
  const std::string token = std::string("\"") + key + "\"";
  const std::string::size_type found = json.find(token, from);
  if (found == std::string::npos) return false;
  const std::string::size_type valueKey = json.find("\"value\"", found);
  if (valueKey == std::string::npos || valueKey > found + 160) return false;
  return numberNear(json, valueKey, "value", value);
}

bool isJsonSpace(char character) {
  return character == ' ' || character == '\t' || character == '\n' || character == '\r';
}

int hexDigit(char character) {
  if (character >= '0' && character <= '9') return character - '0';
  if (character >= 'a' && character <= 'f') return character - 'a' + 10;
  if (character >= 'A' && character <= 'F') return character - 'A' + 10;
  return -1;
}

void appendCodepoint(std::string& out, unsigned int codepoint) {
  if (codepoint < 0x80) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

std::wstring toWide(const std::string& utf8) {
  if (utf8.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  if (size <= 0) return {};
  std::wstring wide(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), size);
  return wide;
}

bool takeObject(const std::string& json, std::string::size_type start, std::string& slice, std::string::size_type& next) {
  if (start >= json.size() || json[start] != '{') return false;
  int depth = 0;
  bool inString = false;
  bool escape = false;
  for (std::string::size_type index = start; index < json.size(); index += 1) {
    const char character = json[index];
    if (inString) {
      if (escape) escape = false;
      else if (character == '\\') escape = true;
      else if (character == '"') inString = false;
      continue;
    }
    if (character == '"') {
      inString = true;
      continue;
    }
    if (character == '{' || character == '[') depth += 1;
    else if (character == '}' || character == ']') {
      depth -= 1;
      if (depth == 0) {
        slice = json.substr(start, index - start + 1);
        next = index + 1;
        return true;
      }
    }
  }
  return false;
}

bool bracketSpan(const std::string& json, std::string::size_type open, std::string::size_type& end) {
  if (open >= json.size() || (json[open] != '[' && json[open] != '{')) return false;
  int depth = 0;
  bool inString = false;
  bool escape = false;
  for (std::string::size_type index = open; index < json.size(); index += 1) {
    const char character = json[index];
    if (inString) {
      if (escape) escape = false;
      else if (character == '\\') escape = true;
      else if (character == '"') inString = false;
      continue;
    }
    if (character == '"') {
      inString = true;
      continue;
    }
    if (character == '{' || character == '[') depth += 1;
    else if (character == '}' || character == ']') {
      depth -= 1;
      if (depth == 0) {
        end = index;
        return true;
      }
    }
  }
  return false;
}

bool jsonString(const std::string& json, std::string::size_type from, const char* key, std::string& value) {
  const std::string token = std::string("\"") + key + "\"";
  const std::string::size_type found = json.find(token, from);
  if (found == std::string::npos) return false;
  const std::string::size_type colon = json.find(':', found + token.size());
  if (colon == std::string::npos || colon > found + token.size() + 8) return false;
  std::string::size_type index = colon + 1;
  while (index < json.size() && isJsonSpace(json[index])) index += 1;
  if (index + 4 <= json.size() && json.compare(index, 4, "null") == 0) return false;
  if (index >= json.size() || json[index] != '"') return false;
  index += 1;
  std::string out;
  while (index < json.size()) {
    const char character = json[index];
    if (character == '\\' && index + 1 < json.size()) {
      const char escaped = json[index + 1];
      if (escaped == 'u' && index + 5 < json.size()) {
        const int h0 = hexDigit(json[index + 2]);
        const int h1 = hexDigit(json[index + 3]);
        const int h2 = hexDigit(json[index + 4]);
        const int h3 = hexDigit(json[index + 5]);
        if (h0 >= 0 && h1 >= 0 && h2 >= 0 && h3 >= 0) {
          appendCodepoint(out, static_cast<unsigned int>((h0 << 12) | (h1 << 8) | (h2 << 4) | h3));
          index += 6;
          continue;
        }
      }
      char decoded = escaped;
      if (escaped == 'n') decoded = '\n';
      else if (escaped == 't') decoded = '\t';
      else if (escaped == 'r') decoded = '\r';
      out.push_back(decoded);
      index += 2;
      continue;
    }
    if (character == '"') break;
    out.push_back(character);
    index += 1;
  }
  while (!out.empty() && isJsonSpace(out.back())) out.pop_back();
  std::string::size_type lead = 0;
  while (lead < out.size() && isJsonSpace(out[lead])) lead += 1;
  if (lead > 0) out.erase(0, lead);
  if (out.empty()) return false;
  value = out;
  return true;
}

bool jsonNumber(const std::string& json, std::string::size_type from, const char* key, double& value) {
  const std::string token = std::string("\"") + key + "\"";
  const std::string::size_type found = json.find(token, from);
  if (found == std::string::npos) return false;
  const std::string::size_type colon = json.find(':', found + token.size());
  if (colon == std::string::npos || colon > found + token.size() + 8) return false;
  std::string::size_type index = colon + 1;
  while (index < json.size() && isJsonSpace(json[index])) index += 1;
  if (index + 4 <= json.size() && json.compare(index, 4, "null") == 0) return false;
  if (index >= json.size()) return false;
  char* end = nullptr;
  value = std::strtod(json.c_str() + index, &end);
  return end != json.c_str() + index;
}

std::string objectAfterKey(const std::string& json, std::string::size_type from, const char* key) {
  const std::string token = std::string("\"") + key + "\"";
  const std::string::size_type found = json.find(token, from);
  if (found == std::string::npos) return {};
  const std::string::size_type colon = json.find(':', found + token.size());
  if (colon == std::string::npos || colon > found + token.size() + 8) return {};
  const std::string::size_type brace = json.find('{', colon + 1);
  if (brace == std::string::npos || brace > colon + 16) return {};
  std::string slice;
  std::string::size_type next = 0;
  if (!takeObject(json, brace, slice, next)) return {};
  return slice;
}

template <typename Fn>
void eachObject(const std::string& json, const char* key, Fn visit) {
  const std::string token = std::string("\"") + key + "\"";
  const std::string::size_type found = json.find(token);
  if (found == std::string::npos) return;
  const std::string::size_type colon = json.find(':', found + token.size());
  if (colon == std::string::npos) return;
  std::string::size_type index = colon + 1;
  while (index < json.size() && isJsonSpace(json[index])) index += 1;
  if (index >= json.size()) return;
  if (json[index] == '{') {
    std::string slice;
    std::string::size_type next = 0;
    if (takeObject(json, index, slice, next)) visit(slice);
    return;
  }
  if (json[index] != '[') return;
  std::string::size_type end = 0;
  if (!bracketSpan(json, index, end)) return;
  std::string::size_type cursor = index + 1;
  while (cursor < end) {
    while (cursor < end && json[cursor] != '{') cursor += 1;
    if (cursor >= end) break;
    std::string slice;
    std::string::size_type next = 0;
    if (!takeObject(json, cursor, slice, next) || next > end) break;
    cursor = next;
    visit(slice);
  }
}

bool parseCode(const std::string& text, int& code) {
  if (text.empty()) return false;
  char* end = nullptr;
  const long value = std::strtol(text.c_str(), &end, 10);
  if (end == text.c_str() || end == nullptr || *end != '\0') return false;
  code = static_cast<int>(value);
  return true;
}

struct ConditionNames {
  std::wstring chinese;
  std::wstring english;
};

std::map<int, ConditionNames>& conditionTable() {
  static std::map<int, ConditionNames> table;
  static bool loaded = false;
  if (loaded) return table;
  loaded = true;
  const std::wstring paths[] = {dataPath(L"local\\weather-conditions.csv"), dataPath(L"weather-conditions.csv"), L"D:\\Source\\Documents\\科學自然\\weather-conditions.csv"};
  std::ifstream input;
  for (const std::wstring& path : paths) {
    input.open(std::filesystem::path(path), std::ios::binary);
    if (input) break;
  }
  if (!input) {
    logMissingResource(paths[0]);
    return table;
  }
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF && static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF) {
      line.erase(0, 3);
    }
    if (line.empty() || line[0] == '#') continue;
    const std::string::size_type comma = line.find(',');
    if (comma == std::string::npos) continue;
    const std::string::size_type next = line.find(',', comma + 1);
    const std::string codeText = line.substr(0, comma);
    const std::string chinese = next == std::string::npos ? line.substr(comma + 1) : line.substr(comma + 1, next - comma - 1);
    std::string english;
    if (next != std::string::npos) {
      const std::string::size_type third = line.find(',', next + 1);
      english = third == std::string::npos ? line.substr(next + 1) : line.substr(next + 1, third - next - 1);
    }
    int code = 0;
    if (!parseCode(codeText, code) || chinese.empty() || chinese == "text_zh") continue;
    table[code] = ConditionNames{toWide(chinese), toWide(english)};
  }
  return table;
}

std::wstring conditionLabel(int code, const std::wstring& apiText) {
  if (code >= 0) {
    const auto& table = conditionTable();
    const auto found = table.find(code);
    if (found != table.end()) {
      const std::wstring& chosen = calendarSettings().language == 1 && !found->second.english.empty() ? found->second.english : found->second.chinese;
      if (!chosen.empty()) return chosen;
    }
  }
  if (!apiText.empty()) return apiText;
  return calendarSettings().language == 1 ? L"Unknown" : L"未知";
}

std::wstring clockText(const std::string& stamp) {
  auto readClock = [](const std::string& text, std::string::size_type at, std::wstring& out) {
    if (at + 5 > text.size()) return false;
    const auto digit = [](char character) { return std::isdigit(static_cast<unsigned char>(character)) != 0; };
    if (!digit(text[at]) || !digit(text[at + 1]) || text[at + 2] != ':' || !digit(text[at + 3]) || !digit(text[at + 4])) return false;
    out.assign({static_cast<wchar_t>(text[at]), static_cast<wchar_t>(text[at + 1]), L':', static_cast<wchar_t>(text[at + 3]), static_cast<wchar_t>(text[at + 4])});
    return true;
  };
  std::wstring out;
  const std::string::size_type marker = stamp.find('T');
  if (marker != std::string::npos && readClock(stamp, marker + 1, out)) return out;
  if (readClock(stamp, 0, out)) return out;
  return {};
}

int qweatherToWmo(int icon) {
  if (icon == 100 || icon == 150) return 0;
  if (icon == 102 || icon == 103 || icon == 151 || icon == 152 || icon == 153) return 1;
  if (icon == 101) return 2;
  if (icon == 104 || icon == 154) return 3;
  if ((icon >= 500 && icon <= 515) || icon == 509 || icon == 510) return 45;
  if (icon == 309) return 51;
  if (icon == 300 || icon == 305) return 61;
  if (icon == 306 || icon == 314 || icon == 399) return 63;
  if (icon >= 307 && icon <= 312) return 65;
  if (icon == 313) return 66;
  if (icon == 400 || icon == 404 || icon == 405 || icon == 406 || icon == 407 || icon == 456 || icon == 457) return 71;
  if (icon == 401) return 73;
  if (icon == 402 || icon == 403 || icon == 409 || icon == 410) return 75;
  if (icon == 302 || icon == 303) return 95;
  if (icon == 304) return 96;
  return 3;
}

bool localDateFromUtc(const std::string& stamp, int& year, int& month, int& day) {
  int hour = 0;
  int minute = 0;
  if (std::sscanf(stamp.c_str(), "%d-%d-%dT%d:%d", &year, &month, &day, &hour, &minute) < 3) return false;
  SYSTEMTIME utc{};
  utc.wYear = static_cast<WORD>(year);
  utc.wMonth = static_cast<WORD>(month);
  utc.wDay = static_cast<WORD>(day);
  utc.wHour = static_cast<WORD>(hour);
  utc.wMinute = static_cast<WORD>(minute);
  SYSTEMTIME local{};
  if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return isValidDate(year, month, day);
  year = local.wYear;
  month = local.wMonth;
  day = local.wDay;
  return isValidDate(year, month, day);
}

std::string base64Url(const std::string& input) {
  static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  const unsigned char* data = reinterpret_cast<const unsigned char*>(input.data());
  const size_t length = input.size();
  for (size_t index = 0; index < length; index += 3) {
    const unsigned int chunk = (static_cast<unsigned int>(data[index]) << 16) | ((index + 1 < length ? data[index + 1] : 0) << 8) | (index + 2 < length ? data[index + 2] : 0);
    out.push_back(table[(chunk >> 18) & 63]);
    out.push_back(table[(chunk >> 12) & 63]);
    if (index + 1 < length) out.push_back(table[(chunk >> 6) & 63]);
    if (index + 2 < length) out.push_back(table[chunk & 63]);
  }
  for (char& character : out) {
    if (character == '+') character = '-';
    else if (character == '/') character = '_';
  }
  return out;
}

bool runHidden(const std::wstring& command) {
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION process{};
  std::wstring line = command;
  if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return false;
  WaitForSingleObject(process.hProcess, 10000);
  DWORD code = 1;
  GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hProcess);
  CloseHandle(process.hThread);
  return code == 0;
}

std::wstring makeJwt(const CalendarSettings& settings) {
  if (settings.qweatherKid.empty() || settings.qweatherIssuer.empty() || settings.qweatherProject.empty()) return {};
  if (settings.qweatherPrivate.find(L"PRIVATE KEY") == std::wstring::npos) return {};
  const std::wstring keyPath = dataPath(L"qweather_private.pem");
  const std::wstring messagePath = dataPath(L"qweather_jwt_msg.bin");
  const std::wstring signaturePath = dataPath(L"qweather_jwt_sig.bin");
  {
    std::ofstream key{std::filesystem::path(keyPath), std::ios::binary};
    if (!key) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, settings.qweatherPrivate.data(), static_cast<int>(settings.qweatherPrivate.size()), nullptr, 0, nullptr, nullptr);
    std::string pem(size, '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, settings.qweatherPrivate.data(), static_cast<int>(settings.qweatherPrivate.size()), pem.data(), size, nullptr, nullptr);
    key << pem;
  }
  const long long issued = static_cast<long long>(std::time(nullptr)) - 30;
  const long long expires = issued + 900;
  auto utf8 = [](const std::wstring& text) {
    std::string out;
    for (wchar_t character : text) {
      if (character < 128) out.push_back(static_cast<char>(character));
    }
    return out;
  };
  const std::string header = base64Url("{\"alg\": \"EdDSA\", \"kid\": \"" + utf8(settings.qweatherKid) + "\"}");
  const std::string payload = base64Url("{\"iss\": \"" + utf8(settings.qweatherIssuer) + "\", \"sub\": \"" + utf8(settings.qweatherProject) + "\", \"iat\": " + std::to_string(issued) + ", \"exp\": " + std::to_string(expires) + "}");
  const std::string signingInput = header + "." + payload;
  {
    std::ofstream message{std::filesystem::path(messagePath), std::ios::binary};
    if (!message) return {};
    message << signingInput;
  }
  const std::wstring openssl = opensslExecutable();
  const DWORD opensslAttributes = GetFileAttributesW(openssl.c_str());
  if (opensslAttributes == INVALID_FILE_ATTRIBUTES || (opensslAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    logMissingResource(openssl);
    return {};
  }
  const std::wstring command = L"\"" + openssl + L"\" pkeyutl -sign -inkey \"" + keyPath + L"\" -rawin -in \"" + messagePath + L"\" -out \"" + signaturePath + L"\"";
  const bool signedOk = runHidden(command);
  std::string signature;
  if (signedOk) {
    std::ifstream input{std::filesystem::path(signaturePath), std::ios::binary};
    signature.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  }
  std::error_code error;
  std::filesystem::remove(std::filesystem::path(messagePath), error);
  std::filesystem::remove(std::filesystem::path(signaturePath), error);
  if (signature.size() != 64) {
    logAccessError(openssl, L"签名失败");
    return {};
  }
  const std::string token = signingInput + "." + base64Url(signature);
  return std::wstring(token.begin(), token.end());
}

std::string qweatherGet(const std::wstring& url, const std::wstring& jwt) {
  if (quotaUsed() >= 40000 || jwt.empty()) return {};
  const std::wstring headers = L"Authorization: Bearer " + jwt + L"\r\n";
  const std::string body = httpGet(url.c_str(), headers.c_str());
  quotaAdd(1);
  return body;
}

bool rejectedBody(const std::string& body) {
  if (body.empty() || body.find("\"status\":401") != std::string::npos || body.find("Unauthorized") != std::string::npos) return true;
  std::string status;
  return jsonString(body, 0, "code", status) && status != "200";
}

bool jsonFlag(const std::string& json, const char* key) {
  const std::string token = std::string("\"") + key + "\"";
  const std::string::size_type found = json.find(token);
  if (found == std::string::npos) return false;
  const std::string::size_type colon = json.find(':', found + token.size());
  if (colon == std::string::npos) return false;
  std::string::size_type index = colon + 1;
  while (index < json.size() && isJsonSpace(json[index])) index += 1;
  return index + 4 <= json.size() && json.compare(index, 4, "true") == 0;
}

std::wstring lookupQweatherCity(const std::wstring& host, const std::wstring& jwt, double latitude, double longitude) {
  static double cachedLatitude = 0;
  static double cachedLongitude = 0;
  static int cachedLanguage = -1;
  static std::wstring cachedName;
  static bool cached = false;
  const int language = calendarSettings().language;
  if (cached && cachedLanguage == language && std::fabs(latitude - cachedLatitude) < 0.02 && std::fabs(longitude - cachedLongitude) < 0.02) return cachedName;
  if (quotaUsed() >= 40000 || jwt.empty() || host.empty()) return {};
  wchar_t url[640];
  std::swprintf(url, 640, L"https://%ls/geo/v2/city/lookup?location=%.2f%%2C%.2f&number=5&lang=%ls", host.c_str(), longitude, latitude, language == 1 ? L"en" : L"zh");
  const std::string body = qweatherGet(url, jwt);
  if (body.empty()) return {};
  if (body.find("\"status\":401") != std::string::npos || body.find("Unauthorized") != std::string::npos) {
    logAccessError(url, L"认证失败");
    return {};
  }
  std::string status;
  if (jsonString(body, 0, "code", status) && status != "200") {
    logAccessError(url, toWide(status).empty() ? L"接口返回异常" : L"接口返回 " + toWide(status));
    return {};
  }
  std::wstring best;
  double bestDistance = 1e12;
  bool any = false;
  eachObject(body, "location", [&](const std::string& item) {
    std::string name;
    if (!jsonString(item, 0, "name", name)) return;
    double distance = 1e9;
    std::string latitudeText;
    std::string longitudeText;
    if (jsonString(item, 0, "lat", latitudeText) && jsonString(item, 0, "lon", longitudeText)) {
      char* latitudeEnd = nullptr;
      char* longitudeEnd = nullptr;
      const double placeLatitude = std::strtod(latitudeText.c_str(), &latitudeEnd);
      const double placeLongitude = std::strtod(longitudeText.c_str(), &longitudeEnd);
      if (latitudeEnd != latitudeText.c_str() && longitudeEnd != longitudeText.c_str()) {
        const double deltaLatitude = placeLatitude - latitude;
        const double deltaLongitude = placeLongitude - longitude;
        distance = deltaLatitude * deltaLatitude + deltaLongitude * deltaLongitude;
      }
    }
    if (!any || distance < bestDistance) {
      any = true;
      bestDistance = distance;
      best = toWide(name);
    }
  });
  if (best.empty()) {
    logAccessError(url, L"没有城市结果");
    return {};
  }
  cached = true;
  cachedLatitude = latitude;
  cachedLongitude = longitude;
  cachedLanguage = language;
  cachedName = best;
  return best;
}

void keepWarning(WeatherSnapshot& snapshot) {
  const WeatherSnapshot previous = currentWeather();
  snapshot.warningText = previous.warningText;
  snapshot.warningCount = previous.warningCount;
  snapshot.warningRed = previous.warningRed;
  snapshot.warningGreen = previous.warningGreen;
  snapshot.warningBlue = previous.warningBlue;
  snapshot.warningNote = previous.warningNote;
}

void readWarningNote(const std::string& body, std::wstring& note) {
  const std::string::size_type found = body.find("\"attributions\"");
  if (found == std::string::npos) return;
  const std::string::size_type colon = body.find(':', found + 14);
  if (colon == std::string::npos) return;
  std::string::size_type index = colon + 1;
  while (index < body.size() && isJsonSpace(body[index])) index += 1;
  if (index >= body.size() || body[index] != '[') return;
  std::string::size_type end = 0;
  if (!bracketSpan(body, index, end)) return;
  std::wstring link;
  std::string::size_type cursor = index + 1;
  while (cursor < end) {
    while (cursor < end && (isJsonSpace(body[cursor]) || body[cursor] == ',')) cursor += 1;
    if (cursor >= end) break;
    std::string text;
    if (body[cursor] == '"') {
      cursor += 1;
      while (cursor < body.size()) {
        if (body[cursor] == '\\' && cursor + 1 < body.size()) {
          text.push_back(body[cursor + 1]);
          cursor += 2;
          continue;
        }
        if (body[cursor] == '"') {
          cursor += 1;
          break;
        }
        text.push_back(body[cursor]);
        cursor += 1;
      }
    } else if (body[cursor] == '{') {
      std::string slice;
      std::string::size_type next = 0;
      if (!takeObject(body, cursor, slice, next)) break;
      cursor = next;
      if (!jsonString(slice, 0, "text", text) && !jsonString(slice, 0, "name", text) && !jsonString(slice, 0, "description", text)) continue;
    } else {
      cursor += 1;
      continue;
    }
    if (text.empty()) continue;
    const bool http = text.rfind("http://", 0) == 0 || text.rfind("https://", 0) == 0;
    if (!http) {
      note = toWide(text);
      return;
    }
    if (link.empty()) link = toWide(text);
  }
  if (note.empty()) note = link;
}

void applyWarning(const std::wstring& host, const std::wstring& jwt, double latitude, double longitude, WeatherSnapshot& snapshot) {
  if (quotaUsed() >= 40000) {
    keepWarning(snapshot);
    return;
  }
  wchar_t url[512];
  std::swprintf(url, 512, L"https://%ls/weatheralert/v1/current/%.2f/%.2f?lang=%ls", host.c_str(), latitude, longitude, calendarSettings().language == 1 ? L"en" : L"zh");
  const std::string body = qweatherGet(url, jwt);
  if (body.empty() || body.find("\"status\":401") != std::string::npos || body.find("Unauthorized") != std::string::npos) {
    if (!body.empty()) logAccessError(url, L"认证失败");
    keepWarning(snapshot);
    return;
  }
  const bool listed = body.find("\"alerts\"") != std::string::npos;
  const bool none = jsonFlag(body, "zeroResult");
  if (!listed && !none) {
    logAccessError(url, L"预警数据无法解析");
    keepWarning(snapshot);
    return;
  }
  std::wstring first;
  int count = 0;
  int red = -1;
  int green = -1;
  int blue = -1;
  eachObject(body, "alerts", [&](const std::string& alert) {
    const std::string message = objectAfterKey(alert, 0, "messageType");
    std::string messageCode;
    if (!message.empty() && jsonString(message, 0, "code", messageCode) && messageCode == "cancel") return;
    std::string headline;
    std::string eventName;
    std::string description;
    jsonString(alert, 0, "headline", headline);
    const std::string event = objectAfterKey(alert, 0, "eventType");
    if (!event.empty()) jsonString(event, 0, "name", eventName);
    if (headline.empty() && eventName.empty()) jsonString(alert, 0, "description", description);
    std::wstring label = !headline.empty() ? toWide(headline) : (!eventName.empty() ? toWide(eventName) : toWide(description));
    if (label.empty()) return;
    if (first.empty()) {
      first = label;
      const std::string color = objectAfterKey(alert, 0, "color");
      double colorRed = 0;
      double colorGreen = 0;
      double colorBlue = 0;
      if (!color.empty() && jsonNumber(color, 0, "red", colorRed) && jsonNumber(color, 0, "green", colorGreen) && jsonNumber(color, 0, "blue", colorBlue) && colorRed >= 0 && colorRed <= 255 && colorGreen >= 0 && colorGreen <= 255 && colorBlue >= 0 && colorBlue <= 255) {
        red = static_cast<int>(std::lround(colorRed));
        green = static_cast<int>(std::lround(colorGreen));
        blue = static_cast<int>(std::lround(colorBlue));
      }
    }
    count += 1;
  });
  snapshot.warningText = first;
  snapshot.warningCount = count;
  snapshot.warningRed = red;
  snapshot.warningGreen = green;
  snapshot.warningBlue = blue;
  if (count > 0) readWarningNote(body, snapshot.warningNote);
}

void applyAstronomy(const std::wstring& host, const std::wstring& jwt, double latitude, double longitude, WeatherSnapshot& snapshot) {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  wchar_t date[16];
  std::swprintf(date, 16, L"%04u%02u%02u", now.wYear, now.wMonth, now.wDay);
  if (quotaUsed() >= 40000) {
    const WeatherSnapshot previous = currentWeather();
    snapshot.sunrise = previous.sunrise;
    snapshot.sunset = previous.sunset;
    snapshot.moonrise = previous.moonrise;
    snapshot.moonset = previous.moonset;
    snapshot.moonPhase = previous.moonPhase;
    return;
  }
  wchar_t sunUrl[640];
  std::swprintf(sunUrl, 640, L"https://%ls/v7/astronomy/sun?location=%.2f%%2C%.2f&date=%ls", host.c_str(), longitude, latitude, date);
  const std::string sun = qweatherGet(sunUrl, jwt);
  if (rejectedBody(sun)) {
    if (!sun.empty()) {
      std::string status;
      if (sun.find("\"status\":401") != std::string::npos || sun.find("Unauthorized") != std::string::npos) logAccessError(sunUrl, L"认证失败");
      else if (jsonString(sun, 0, "code", status)) logAccessError(sunUrl, L"接口返回 " + toWide(status));
      else logAccessError(sunUrl, L"日出日落无法解析");
    }
    const WeatherSnapshot previous = currentWeather();
    snapshot.sunrise = previous.sunrise;
    snapshot.sunset = previous.sunset;
  } else {
    std::string sunrise;
    std::string sunset;
    if (jsonString(sun, 0, "sunrise", sunrise)) snapshot.sunrise = clockText(sunrise);
    if (jsonString(sun, 0, "sunset", sunset)) snapshot.sunset = clockText(sunset);
  }
  if (quotaUsed() >= 40000) {
    const WeatherSnapshot previous = currentWeather();
    snapshot.moonrise = previous.moonrise;
    snapshot.moonset = previous.moonset;
    snapshot.moonPhase = previous.moonPhase;
    return;
  }
  wchar_t moonUrl[640];
  std::swprintf(moonUrl, 640, L"https://%ls/v7/astronomy/moon?location=%.2f%%2C%.2f&date=%ls&lang=%ls", host.c_str(), longitude, latitude, date, calendarSettings().language == 1 ? L"en" : L"zh");
  const std::string moon = qweatherGet(moonUrl, jwt);
  if (rejectedBody(moon)) {
    if (!moon.empty()) {
      std::string status;
      if (moon.find("\"status\":401") != std::string::npos || moon.find("Unauthorized") != std::string::npos) logAccessError(moonUrl, L"认证失败");
      else if (jsonString(moon, 0, "code", status)) logAccessError(moonUrl, L"接口返回 " + toWide(status));
      else logAccessError(moonUrl, L"月相数据无法解析");
    }
    const WeatherSnapshot previous = currentWeather();
    snapshot.moonrise = previous.moonrise;
    snapshot.moonset = previous.moonset;
    snapshot.moonPhase = previous.moonPhase;
    return;
  }
  std::string moonrise;
  std::string moonset;
  if (jsonString(moon, 0, "moonrise", moonrise)) snapshot.moonrise = clockText(moonrise);
  if (jsonString(moon, 0, "moonset", moonset)) snapshot.moonset = clockText(moonset);
  std::string bestName;
  int bestDelta = 100000;
  bool havePhase = false;
  eachObject(moon, "moonPhase", [&](const std::string& item) {
    std::string name;
    if (!jsonString(item, 0, "name", name)) return;
    int delta = 100000;
    std::string stamp;
    if (jsonString(item, 0, "fxTime", stamp)) {
      const std::string::size_type marker = stamp.find('T');
      const std::string::size_type at = marker == std::string::npos ? 0 : marker + 1;
      if (at + 1 < stamp.size() && std::isdigit(static_cast<unsigned char>(stamp[at])) && std::isdigit(static_cast<unsigned char>(stamp[at + 1]))) {
        const int hour = (stamp[at] - '0') * 10 + (stamp[at + 1] - '0');
        if (hour >= 0 && hour <= 23) {
          int difference = std::abs(hour - static_cast<int>(now.wHour));
          if (difference > 12) difference = 24 - difference;
          delta = difference;
        }
      }
    }
    if (!havePhase || delta < bestDelta) {
      havePhase = true;
      bestDelta = delta;
      bestName = name;
    }
  });
  if (havePhase) snapshot.moonPhase = toWide(bestName);
}

bool readQweather(double latitude, double longitude, const std::wstring& host, const std::wstring& jwt, WeatherSnapshot& snapshot) {
  if (quotaUsed() > 40000 - 2) return false;
  wchar_t currentUrl[512];
  wchar_t dailyUrl[512];
  const wchar_t* language = calendarSettings().language == 1 ? L"en" : L"zh";
  std::swprintf(currentUrl, 512, L"https://%ls/weather/v1/current/%.2f/%.2f?lang=%ls", host.c_str(), latitude, longitude, language);
  std::swprintf(dailyUrl, 512, L"https://%ls/weather/v1/daily/%.2f/%.2f?lang=%ls", host.c_str(), latitude, longitude, language);
  const std::string current = qweatherGet(currentUrl, jwt);
  if (current.find("\"status\":401") != std::string::npos || current.find("Unauthorized") != std::string::npos) {
    logAccessError(currentUrl, L"认证失败");
    return false;
  }
  const std::string daily = qweatherGet(dailyUrl, jwt);
  double temperature = 0;
  if (!objectNumber(current, 0, "temperature", temperature)) {
    if (!current.empty()) logAccessError(currentUrl, L"实况数据无法解析");
    return false;
  }
  snapshot.temperature = temperature;
  const std::string condition = objectAfterKey(current, 0, "condition");
  std::string iconText;
  std::string phenomenonText;
  int phenomenonCode = -1;
  if (!condition.empty() && jsonString(condition, 0, "code", iconText) && parseCode(iconText, phenomenonCode)) {
    snapshot.conditionCode = phenomenonCode;
    snapshot.code = qweatherToWmo(phenomenonCode);
  } else snapshot.code = 3;
  if (!condition.empty()) jsonString(condition, 0, "text", phenomenonText);
  snapshot.conditionText = condition.empty() ? L"未知" : conditionLabel(snapshot.conditionCode, toWide(phenomenonText));
  double humidity = 0;
  if (numberNear(current, 0, "humidity", humidity)) snapshot.humidity = static_cast<int>(std::lround(humidity <= 1.5 ? humidity * 100.0 : humidity));
  else snapshot.humidity = -1;
  double speed = 0;
  double direction = 0;
  const std::string::size_type windAt = current.find("\"wind\"");
  if (windAt != std::string::npos && objectNumber(current, windAt, "speed", speed)) {
    const std::string::size_type unitAt = current.find("\"unit\"", windAt);
    const bool kilometers = unitAt != std::string::npos && unitAt < windAt + 120 && current.find("km/h", unitAt) < unitAt + 24;
    snapshot.windSpeed = static_cast<int>(std::lround(kilometers ? speed : speed * 3.6));
  } else snapshot.windSpeed = 0;
  snapshot.windDirection = windAt != std::string::npos && numberNear(current, windAt, "degree", direction) ? static_cast<int>(std::lround(direction)) : 0;

  snapshot.dayCount = 0;
  const std::string::size_type daysKey = daily.find("\"days\"");
  const std::string::size_type arrayOpen = daysKey == std::string::npos ? std::string::npos : daily.find('[', daysKey);
  std::string::size_type cursor = arrayOpen == std::string::npos ? daily.size() : arrayOpen + 1;
  while (snapshot.dayCount < 7 && cursor < daily.size()) {
    const std::string::size_type start = daily.find('{', cursor);
    if (start == std::string::npos) break;
    int depth = 0;
    bool inString = false;
    bool escape = false;
    std::string::size_type end = std::string::npos;
    for (std::string::size_type index = start; index < daily.size(); index += 1) {
      const char character = daily[index];
      if (inString) {
        if (escape) escape = false;
        else if (character == '\\') escape = true;
        else if (character == '"') inString = false;
        continue;
      }
      if (character == '"') {
        inString = true;
        continue;
      }
      if (character == '{' || character == '[') depth += 1;
      else if (character == '}' || character == ']') {
        depth -= 1;
        if (depth == 0) {
          end = index;
          break;
        }
      }
    }
    if (end == std::string::npos) break;
    const std::string slice = daily.substr(start, end - start + 1);
    cursor = end + 1;
    std::string stamp;
    int year = 0;
    int month = 0;
    int day = 0;
    double high = 0;
    double low = 0;
    if (!jsonString(slice, 0, "forecastStartTime", stamp) || !localDateFromUtc(stamp, year, month, day)) break;
    if (!objectNumber(slice, 0, "temperatureMax", high) || !objectNumber(slice, 0, "temperatureMin", low)) break;
    ForecastDay& forecast = snapshot.days[snapshot.dayCount];
    forecast.year = year;
    forecast.month = month;
    forecast.day = day;
    forecast.weekday = weekdayIndex(year, month, day);
    const std::string daytime = objectAfterKey(slice, 0, "daytime");
    const std::string& phenomenon = daytime.empty() ? slice : daytime;
    std::string dayIcon;
    std::string dayText;
    int dayCode = -1;
    if (jsonString(phenomenon, 0, "code", dayIcon) && parseCode(dayIcon, dayCode)) {
      forecast.conditionCode = dayCode;
      forecast.code = qweatherToWmo(dayCode);
    } else forecast.code = snapshot.code;
    jsonString(phenomenon, 0, "text", dayText);
    if (dayCode >= 0 || !dayText.empty()) forecast.conditionText = conditionLabel(dayCode, toWide(dayText));
    const std::string::size_type dayWind = slice.find("\"wind\"");
    double daySpeed = 0;
    double dayDirection = 0;
    if (dayWind != std::string::npos && objectNumber(slice, dayWind, "speed", daySpeed)) {
      const std::string::size_type unitAt = slice.find("\"unit\"", dayWind);
      const bool kilometers = unitAt != std::string::npos && unitAt < dayWind + 120 && slice.find("km/h", unitAt) < unitAt + 24;
      forecast.windSpeed = static_cast<int>(std::lround(kilometers ? daySpeed : daySpeed * 3.6));
    } else forecast.windSpeed = 0;
    forecast.windDirection = dayWind != std::string::npos && numberNear(slice, dayWind, "degree", dayDirection) ? static_cast<int>(std::lround(dayDirection)) : 0;
    forecast.high = static_cast<int>(std::lround(high));
    forecast.low = static_cast<int>(std::lround(low));
    snapshot.dayCount += 1;
  }
  if (snapshot.dayCount <= 0) {
    if (!daily.empty() && daily.find("\"status\":401") == std::string::npos && daily.find("Unauthorized") == std::string::npos) logAccessError(dailyUrl, L"逐日数据无法解析");
    else if (daily.find("\"status\":401") != std::string::npos || daily.find("Unauthorized") != std::string::npos) logAccessError(dailyUrl, L"认证失败");
    return false;
  }
  if (quotaUsed() < 40000) {
    wchar_t airUrl[512];
    std::swprintf(airUrl, 512, L"https://%ls/airquality/v1/current/%.2f/%.2f", host.c_str(), latitude, longitude);
    const std::string air = qweatherGet(airUrl, jwt);
    const std::string::size_type aqiAt = air.find("\"aqi\"");
    double aqi = 0;
    bool haveAqi = false;
    if (aqiAt != std::string::npos) {
      const std::string::size_type colon = air.find(':', aqiAt);
      if (colon != std::string::npos && colon + 1 < air.size() && air[colon + 1] != '{') haveAqi = numberNear(air, aqiAt, "aqi", aqi);
      else haveAqi = objectNumber(air, aqiAt, "aqi", aqi) || numberNear(air, aqiAt, "value", aqi);
    }
    snapshot.aqi = haveAqi ? static_cast<int>(std::lround(aqi)) : -1;
  }
  applyWarning(host, jwt, latitude, longitude, snapshot);
  applyAstronomy(host, jwt, latitude, longitude, snapshot);
  return true;
}

void publishWeather(WeatherSnapshot next) {
  next.ready = true;
  SYSTEMTIME now{};
  GetLocalTime(&now);
  next.updatedHour = now.wHour;
  next.updatedMinute = now.wMinute;
  std::lock_guard<std::mutex> lock(weatherMutex);
  weather = next;
}

void refreshWeather() {
  double latitude = 0;
  double longitude = 0;
  std::wstring city;
  if (!readLocation(latitude, longitude, city)) return;
  const std::wstring chinese = chineseCityName(latitude, longitude, city);
  if (!chinese.empty()) city = chinese;
  const CalendarSettings settings = calendarSettings();
  const std::wstring host = trimCopy(settings.qweatherHost);
  const std::wstring jwt = makeJwt(settings);
  std::wstring geoName;
  if (!host.empty() && !jwt.empty() && quotaUsed() <= 40000 - 3) geoName = lookupQweatherCity(host, jwt, latitude, longitude);
  if (!geoName.empty()) city = geoName;
  WeatherSnapshot next;
  bool loaded = false;
  if (!host.empty() && !jwt.empty()) loaded = readQweather(latitude, longitude, host, jwt, next);
  if (!loaded && !readForecast(latitude, longitude, next)) return;
  next.city = city;
  publishWeather(next);
}

void weatherLoop() {
  while (!stopRequested.load()) {
    refreshWeather();
    refreshRequested.store(false);
    for (int second = 0; second < 30 * 60 && !stopRequested.load() && !refreshRequested.load(); second += 1) Sleep(1000);
  }
}

}

const wchar_t* weatherDescription(int code) {
  if (calendarSettings().language == 1) {
    if (code == 0) return L"Sunny";
    if (code == 1) return L"Mostly sunny";
    if (code == 2) return L"Cloudy";
    if (code == 3) return L"Overcast";
    if (code == 45 || code == 48) return L"Fog";
    if (code >= 51 && code <= 57) return L"Drizzle";
    if (code == 61 || code == 80) return L"Light rain";
    if (code == 63 || code == 81) return L"Rain";
    if (code == 65 || code == 82) return L"Heavy rain";
    if (code == 66 || code == 67) return L"Freezing rain";
    if (code == 71 || code == 85) return L"Light snow";
    if (code == 73) return L"Snow";
    if (code == 75 || code == 86) return L"Heavy snow";
    if (code == 77) return L"Snow grains";
    if (code == 95) return L"Thunderstorm";
    if (code == 96 || code == 99) return L"Thunderstorm";
    return L"Cloudy";
  }
  if (code == 0) return L"晴";
  if (code == 1) return L"大部晴";
  if (code == 2) return L"多云";
  if (code == 3) return L"阴";
  if (code == 45 || code == 48) return L"雾";
  if (code >= 51 && code <= 57) return L"毛毛雨";
  if (code == 61 || code == 80) return L"小雨";
  if (code == 63 || code == 81) return L"中雨";
  if (code == 65 || code == 82) return L"大雨";
  if (code == 66 || code == 67) return L"冻雨";
  if (code == 71 || code == 85) return L"小雪";
  if (code == 73) return L"中雪";
  if (code == 75 || code == 86) return L"大雪";
  if (code == 77) return L"雪粒";
  if (code == 95) return L"雷雨";
  if (code == 96 || code == 99) return L"雷阵雨";
  return L"多云";
}

std::wstring windDescription(int speedKmh, int directionDegrees) {
  const bool english = calendarSettings().language == 1;
  const wchar_t* names[] = {L"北", L"东北", L"东", L"东南", L"南", L"西南", L"西", L"西北"};
  const wchar_t* englishNames[] = {L"N", L"NE", L"E", L"SE", L"S", L"SW", L"W", L"NW"};
  int index = static_cast<int>(std::floor((directionDegrees + 22.5) / 45.0)) % 8;
  if (index < 0) index += 8;
  const int limits[] = {1, 6, 12, 20, 29, 39, 50, 62, 75, 89, 103, 118};
  int level = 12;
  for (int step = 0; step < 12; step += 1) {
    if (speedKmh < limits[step]) {
      level = step;
      break;
    }
  }
  wchar_t text[32];
  if (english) std::swprintf(text, 32, L"%ls %d", englishNames[index], level);
  else std::swprintf(text, 32, L"%ls风%d级", names[index], level);
  return text;
}

const wchar_t* airQualityLabel(int aqi) {
  const bool english = calendarSettings().language == 1;
  if (aqi < 0) return L"—";
  if (aqi <= 50) return english ? L"Good" : L"优";
  if (aqi <= 100) return english ? L"Fair" : L"良";
  if (aqi <= 150) return english ? L"Mild" : L"轻度";
  if (aqi <= 200) return english ? L"Poor" : L"中度";
  if (aqi <= 300) return english ? L"Severe" : L"重度";
  return english ? L"Hazard" : L"严重";
}

void requestWeatherRefresh() {
  refreshRequested.store(true);
}

void startWeatherUpdates() {
  if (worker.joinable()) return;
  stopRequested.store(false);
  worker = std::thread(weatherLoop);
}

void stopWeatherUpdates() {
  stopRequested.store(true);
  if (worker.joinable()) worker.join();
}

WeatherSnapshot currentWeather() {
  std::lock_guard<std::mutex> lock(weatherMutex);
  return weather;
}
