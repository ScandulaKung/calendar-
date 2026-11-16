#include "china_cities.hpp"
#include "log.hpp"
#include "paths.hpp"

#define UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct Place {
  std::wstring name;
  std::wstring province;
  std::string key;
  double latitude = 0;
  double longitude = 0;
};

std::vector<Place> places;
bool listReady = false;

std::wstring utf8ToWide(const std::string& text) {
  if (text.empty()) return L"";
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (size <= 0) return L"";
  std::wstring wide(size, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
  return wide;
}

std::string wideToUtf8(const std::wstring& text) {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string utf8(size, '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr);
  return utf8;
}

std::string englishKey(const std::string& text) {
  std::string key;
  for (unsigned char character : text) {
    if (character >= 'A' && character <= 'Z') key.push_back(static_cast<char>(character - 'A' + 'a'));
    else if ((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9')) key.push_back(static_cast<char>(character));
  }
  return key;
}

std::wstring stripAdminSuffix(std::wstring name) {
  const wchar_t* suffixes[] = {L"特别行政区", L"自治州", L"自治区", L"地区", L"新区", L"市", L"区", L"县", L"省"};
  for (const wchar_t* suffix : suffixes) {
    const std::size_t length = std::wcslen(suffix);
    if (name.size() > length && name.compare(name.size() - length, length, suffix) == 0) {
      name.resize(name.size() - length);
      return name;
    }
  }
  return name;
}

std::vector<std::string> splitCsv(const std::string& line) {
  std::vector<std::string> fields;
  std::string field;
  bool quoted = false;
  for (std::size_t index = 0; index < line.size(); index += 1) {
    const char character = line[index];
    if (quoted) {
      if (character == '"') {
        if (index + 1 < line.size() && line[index + 1] == '"') {
          field.push_back('"');
          index += 1;
        } else {
          quoted = false;
        }
      } else {
        field.push_back(character);
      }
    } else if (character == '"') {
      quoted = true;
    } else if (character == ',') {
      fields.push_back(field);
      field.clear();
    } else if (character != '\r') {
      field.push_back(character);
    }
  }
  fields.push_back(field);
  return fields;
}

bool parseDouble(const std::string& text, double& value) {
  if (text.empty()) return false;
  char* end = nullptr;
  value = std::strtod(text.c_str(), &end);
  return end != text.c_str() && *end == '\0';
}

double kilometersBetween(double latitude, double longitude, const Place& place) {
  const double latitudeRadians = latitude * 0.017453292519943295;
  const double east = (place.longitude - longitude) * std::cos(latitudeRadians) * 111.32;
  const double north = (place.latitude - latitude) * 110.574;
  return std::sqrt(east * east + north * north);
}

bool fileExists(const std::wstring& path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring exeDirectory() {
  wchar_t buffer[MAX_PATH];
  const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return L"";
  std::wstring path(buffer, length);
  const std::size_t slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return L"";
  return path.substr(0, slash);
}

void ensureCityList() {
  if (listReady) return;
  static bool attempted = false;
  if (attempted) return;
  attempted = true;
  const std::wstring directory = exeDirectory();
  const wchar_t* relative[] = {
    L"\\local\\China-City-List-latest.csv",
    L"\\LocationList-master\\China-City-List-latest.csv",
    L"\\..\\local\\China-City-List-latest.csv",
    L"\\..\\LocationList-master\\China-City-List-latest.csv",
    L"\\..\\..\\local\\China-City-List-latest.csv",
    L"\\..\\..\\LocationList-master\\China-City-List-latest.csv",
    L"\\..\\..\\..\\local\\China-City-List-latest.csv",
    L"\\..\\..\\..\\LocationList-master\\China-City-List-latest.csv",
  };
  for (const wchar_t* suffix : relative) {
    if (!directory.empty() && loadChinaCityList(directory + suffix)) return;
  }
  if (loadChinaCityList(dataPath(L"local\\China-City-List-latest.csv"))) return;
  if (loadChinaCityList(dataPath(L"LocationList-master\\China-City-List-latest.csv"))) return;
  logMissingResource(dataPath(L"local\\China-City-List-latest.csv"));
}

const Place* closestNamed(double latitude, double longitude, const std::wstring& name, const std::string& key, double limit) {
  const Place* found = nullptr;
  double best = limit;
  for (const Place& place : places) {
    const bool named = (!name.empty() && place.name == name) || (!key.empty() && place.key == key);
    if (!named) continue;
    const double distance = kilometersBetween(latitude, longitude, place);
    if (distance <= best) {
      best = distance;
      found = &place;
    }
  }
  return found;
}

}

bool loadChinaCityList(const std::wstring& path) {
  if (!fileExists(path)) return false;
  std::ifstream input(std::filesystem::path(path), std::ios::binary);
  if (!input) return false;
  std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF && static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) {
    bytes.erase(0, 3);
  }
  std::vector<Place> loaded;
  std::size_t lineStart = 0;
  while (lineStart < bytes.size()) {
    std::size_t lineEnd = bytes.find('\n', lineStart);
    if (lineEnd == std::string::npos) lineEnd = bytes.size();
    const std::vector<std::string> fields = splitCsv(bytes.substr(lineStart, lineEnd - lineStart));
    lineStart = lineEnd + 1;
    if (fields.size() < 13) continue;
    double latitude = 0;
    double longitude = 0;
    if (!parseDouble(fields[11], latitude) || !parseDouble(fields[12], longitude)) continue;
    const std::wstring name = utf8ToWide(fields[2]);
    if (name.empty()) continue;
    loaded.push_back(Place{name, utf8ToWide(fields[7]), englishKey(fields[1]), latitude, longitude});
  }
  if (loaded.empty()) return false;
  places = std::move(loaded);
  listReady = true;
  return true;
}

std::wstring chineseCityName(double latitude, double longitude, const std::wstring& hint) {
  ensureCityList();
  if (!listReady) return L"";
  const std::wstring trimmedHint = stripAdminSuffix(hint);
  const std::string key = englishKey(wideToUtf8(hint));
  if (const Place* named = closestNamed(latitude, longitude, hint, key, 200.0)) return named->name;
  if (trimmedHint != hint) {
    if (const Place* named = closestNamed(latitude, longitude, trimmedHint, {}, 200.0)) return named->name;
  }
  const Place* nearest = nullptr;
  double best = 100.0;
  for (const Place& place : places) {
    const double distance = kilometersBetween(latitude, longitude, place);
    if (distance <= best) {
      best = distance;
      nearest = &place;
    }
  }
  return nearest == nullptr ? L"" : nearest->name;
}

bool chinaCityCenter(const std::wstring& province, const std::wstring& city, double& latitude, double& longitude) {
  ensureCityList();
  if (!listReady) return false;
  const std::wstring cityName = stripAdminSuffix(city);
  const std::wstring provinceName = stripAdminSuffix(province);
  if (cityName.empty()) return false;
  for (const Place& place : places) {
    if (place.name != cityName) continue;
    if (!provinceName.empty() && stripAdminSuffix(place.province) != provinceName) continue;
    latitude = place.latitude;
    longitude = place.longitude;
    return true;
  }
  return false;
}
