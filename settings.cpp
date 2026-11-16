#include "settings.hpp"
#include "extra.hpp"
#include "log.hpp"
#include "paths.hpp"
#include "theme.hpp"
#include "voice.hpp"

#include "date_calc.hpp"
#include "lunar.hpp"
#include "weather.hpp"

#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>

#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr int kClose = 1;
constexpr int kPageBase = 10;
constexpr int kLaunch = 20;
constexpr int kHour24 = 21;
constexpr int kZodiacLichun = 22;
constexpr int kZodiacChuyi = 23;
constexpr int kWeekSunday = 24;
constexpr int kWeekMonday = 25;
constexpr int kFloat = 26;
constexpr int kAlert = 27;
constexpr int kFont = 28;
constexpr int kAlignLeft = 29;
constexpr int kAlignCenter = 30;
constexpr int kAlignRight = 31;
constexpr int kModeSingle = 32;
constexpr int kModeDual = 33;
constexpr int kFormatBox = 34;
constexpr int kFormatHelp = 35;
constexpr int kFormat2Box = 36;
constexpr int kSwitchUp = 37;
constexpr int kSwitchSide = 38;
constexpr int kTimeAnn = 39;
constexpr int kHourly = 40;
constexpr int kHalf = 41;
constexpr int kTen = 65;
constexpr int kMinuteAnn = 66;
constexpr int kWeatherAnn = 42;
constexpr int kQuiet = 43;
constexpr int kFullSound = 44;
constexpr int kSyncNow = 45;
constexpr int kSetTime = 46;
constexpr int kPush = 47;
constexpr int kMenuFormat1 = 50;
constexpr int kMenuFormat2 = 51;
constexpr int kMenuSync = 52;
constexpr int kMenuHour = 53;
constexpr int kMenuMinute = 54;
constexpr int kMenuSecond = 55;
constexpr int kMenuQuietFrom = 56;
constexpr int kMenuQuietTo = 57;
constexpr int kMenuDpi = 58;
constexpr int kMenuLanguage = 67;
constexpr int kThemeNight = 60;
constexpr int kThemeDay = 61;
constexpr int kThemeImage = 62;
constexpr int kPickImage = 63;
constexpr int kQWeather = 64;
constexpr int kConfirm = 68;
constexpr int kOpacity = 69;
constexpr int kDock = 70;
constexpr int kCalendarFont = 71;
constexpr int kShuffle = 72;
constexpr int kOrder = 76;
constexpr int kMenuInterval = 77;
constexpr int kCropLeft = 73;
constexpr int kCropCenter = 74;
constexpr int kCropRight = 75;

std::wstring changelogText();
std::wstring fontSummary(const std::wstring& face, int points, bool bold, bool italic);
constexpr int kMenuBlock = 1900;
constexpr int kMenuItem = 2000;
constexpr int kEditHover = 1001;
constexpr int kEditSwitch1 = 1002;
constexpr int kEditSwitch2 = 1003;
constexpr int kEditFormat = 1004;
constexpr int kEditDate = 1005;

CalendarSettings state;
bool rotationShuffle = false;
bool rotationOrder = false;
int rotationInterval = 10;
std::wstring rotationImage;

void rememberRotationClock() {
  rotationShuffle = state.backgroundShuffle;
  rotationOrder = state.backgroundOrder;
  rotationInterval = backgroundIntervalMinutes(state.backgroundInterval);
  rotationImage = state.backgroundImage;
}

bool rotationClockChanged() {
  return rotationShuffle != state.backgroundShuffle || rotationOrder != state.backgroundOrder || rotationInterval != backgroundIntervalMinutes(state.backgroundInterval) || rotationImage != state.backgroundImage;
}

const wchar_t* S(const wchar_t* zh, const wchar_t* en) {
  return state.language == 1 ? en : zh;
}
bool ready = false;
HWND settingsWindow = nullptr;
HWND hoverEdit = nullptr;
HWND switch1Edit = nullptr;
HWND switch2Edit = nullptr;
HWND formatEdit = nullptr;
HWND dateEdit = nullptr;
HFONT editFont = nullptr;
HBRUSH editBrush = nullptr;
float settingsScale = 1.f;
constexpr float kDesignWidth = kPanelWidth;
constexpr float kDesignHeight = kPanelHeight;
constexpr float kSideWidth = 160.f;
int page = 0;
int scroll = 0;
bool draggingOpacity = false;
Gdiplus::RectF opacityTrack;
int contentBottom = 0;
int openMenu = 0;
int menuScroll = 0;
bool formatSynced = true;
std::wstring statusText;

struct Hit {
  int id = 0;
  Gdiplus::RectF rect;
};

struct EditPlace {
  bool show = false;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

std::vector<Hit> hits;
EditPlace hoverPlace;
EditPlace switch1Place;
EditPlace switch2Place;
EditPlace formatPlace;
EditPlace datePlace;
Gdiplus::RectF menuAnchor;

float dp(float value) {
  return value * settingsScale;
}

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

std::wstring settingsPath() {
  return dataPath(L"calendar_settings.ini");
}

void loadSettings() {
  std::ifstream input{std::filesystem::path(settingsPath())};
  if (!input) return;
  bool sawAnnounceMode = false;
  bool sawDockFont = false;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::string::size_type split = line.find('=');
    if (split == std::string::npos) continue;
    const std::string key = line.substr(0, split);
    const std::string value = line.substr(split + 1);
    const bool on = value == "1";
    if (key == "launch") state.launchAtStartup = on;
    else if (key == "hour24") state.hour24 = on;
    else if (key == "zodiac") state.zodiacFromLichun = on;
    else if (key == "monday") state.weekStartsMonday = on;
    else if (key == "float") state.showWeatherFloat = on;
    else if (key == "hover") state.hoverSeconds = utf8ToWide(value);
    else if (key == "alert") state.weatherAlert = on;
    else if (key == "align") state.clockAlign = std::atoi(value.c_str());
    else if (key == "dual") state.dualScreen = on;
    else if (key == "format1") state.formatPrimary = utf8ToWide(value);
    else if (key == "format2") state.formatSecondary = utf8ToWide(value);
    else if (key == "switch1") state.switchPrimary = utf8ToWide(value);
    else if (key == "switch2") state.switchSecondary = utf8ToWide(value);
    else if (key == "switchUp") state.switchUpward = on;
    else if (key == "font") state.clockFont = utf8ToWide(value);
    else if (key == "fontSize") state.clockFontSize = std::atoi(value.c_str());
    else if (key == "fontBold") state.clockFontBold = on;
    else if (key == "fontItalic") state.clockFontItalic = on;
    else if (key == "dockFont") {
      state.dockFont = utf8ToWide(value);
      sawDockFont = true;
    }
    else if (key == "dockFontSize") state.dockFontSize = std::atoi(value.c_str());
    else if (key == "dockFontBold") state.dockFontBold = on;
    else if (key == "dockFontItalic") state.dockFontItalic = on;
    else if (key == "timeAnn") state.timeAnnounce = on;
    else if (key == "hourly") state.announceHourly = on;
    else if (key == "annMode") {
      state.announceMode = std::atoi(value.c_str());
      if (state.announceMode < 0 || state.announceMode > 3) state.announceMode = 0;
      sawAnnounceMode = true;
    }
    else if (key == "alarm") state.alarmEnabled = on;
    else if (key == "alarmH") state.alarmHour = std::atoi(value.c_str());
    else if (key == "alarmM") state.alarmMinute = std::atoi(value.c_str());
    else if (key == "alarmFile") state.alarmVoice = utf8ToWide(value);
    else if (key == "timerH") state.timerHours = std::atoi(value.c_str());
    else if (key == "timerM") state.timerMinutes = std::atoi(value.c_str());
    else if (key == "timerS") state.timerSeconds = std::atoi(value.c_str());
    else if (key == "timerFile") state.timerVoice = utf8ToWide(value);
    else if (key == "weatherAnn") state.weatherAnnounce = on;
    else if (key == "quiet") state.quietMode = on;
    else if (key == "quietFrom") state.quietFrom = std::atoi(value.c_str());
    else if (key == "quietTo") state.quietTo = std::atoi(value.c_str());
    else if (key == "fullSound") state.soundInFullscreen = on;
    else if (key == "sync") state.syncMode = std::atoi(value.c_str());
    else if (key == "cy") state.customYear = std::atoi(value.c_str());
    else if (key == "cm") state.customMonth = std::atoi(value.c_str());
    else if (key == "cd") state.customDay = std::atoi(value.c_str());
    else if (key == "ch") state.customHour = std::atoi(value.c_str());
    else if (key == "cmin") state.customMinute = std::atoi(value.c_str());
    else if (key == "cs") state.customSecond = std::atoi(value.c_str());
    else if (key == "push") state.receivePush = on;
    else if (key == "dpi") state.dpiMode = std::atoi(value.c_str());
    else if (key == "theme") state.appearance = std::atoi(value.c_str());
    else if (key == "lang") state.language = std::atoi(value.c_str()) == 1 ? 1 : 0;
    else if (key == "opacity") state.windowOpacity = std::atoi(value.c_str());
    else if (key == "dock") state.dockClock = on;
    else if (key == "shuffle") state.backgroundShuffle = on;
    else if (key == "order") state.backgroundOrder = on;
    else if (key == "orderMin") state.backgroundInterval = std::atoi(value.c_str());
    else if (key == "orderAt") state.backgroundOrderStart = std::strtoll(value.c_str(), nullptr, 10);
    else if (key == "crop") state.backgroundCrop = std::atoi(value.c_str());
    else if (key == "image") state.backgroundImage = utf8ToWide(value);
    else if (key == "qhost") state.qweatherHost = utf8ToWide(value);
    else if (key == "qkid" || key == "qkey") state.qweatherKid = utf8ToWide(value);
    else if (key == "qiss") state.qweatherIssuer = utf8ToWide(value);
    else if (key == "qsub") state.qweatherProject = utf8ToWide(value);
    else if (key == "qskip") state.qweatherSkip = on;
  }
  if (!sawAnnounceMode) state.announceMode = state.announceHourly ? 0 : 1;
  else state.announceHourly = state.announceMode == 0;
  if (state.alarmHour < 0 || state.alarmHour > 23) state.alarmHour = 7;
  if (state.alarmMinute < 0 || state.alarmMinute > 59) state.alarmMinute = 0;
  if (state.timerHours < 0 || state.timerHours > 23) state.timerHours = 0;
  if (state.timerMinutes < 0 || state.timerMinutes > 59) state.timerMinutes = 5;
  if (state.timerSeconds < 0 || state.timerSeconds > 59) state.timerSeconds = 0;
  if (state.windowOpacity < 40 || state.windowOpacity > 100) state.windowOpacity = 100;
  if (state.backgroundCrop < 0 || state.backgroundCrop > 2) state.backgroundCrop = 1;
  state.backgroundInterval = backgroundIntervalMinutes(state.backgroundInterval);
  if (state.backgroundOrder && state.backgroundShuffle) state.backgroundShuffle = false;
  if (!sawDockFont || state.dockFont.empty()) state.dockFont = state.clockFont;
  if (state.clockFontSize < 8 || state.clockFontSize > 72) state.clockFontSize = 0;
  if (state.dockFontSize < 8 || state.dockFontSize > 72) state.dockFontSize = 0;
}

void saveSettings() {
  std::ofstream output{std::filesystem::path(settingsPath()), std::ios::binary};
  if (!output) return;
  auto write = [&](const char* key, const std::string& value) { output << key << '=' << value << '\n'; };
  auto flag = [&](const char* key, bool value) { write(key, value ? "1" : "0"); };
  auto number = [&](const char* key, int value) { write(key, std::to_string(value)); };
  flag("launch", state.launchAtStartup);
  flag("hour24", state.hour24);
  flag("zodiac", state.zodiacFromLichun);
  flag("monday", state.weekStartsMonday);
  flag("float", state.showWeatherFloat);
  write("hover", wideToUtf8(state.hoverSeconds));
  flag("alert", state.weatherAlert);
  number("align", state.clockAlign);
  flag("dual", state.dualScreen);
  write("format1", wideToUtf8(state.formatPrimary));
  write("format2", wideToUtf8(state.formatSecondary));
  write("switch1", wideToUtf8(state.switchPrimary));
  write("switch2", wideToUtf8(state.switchSecondary));
  flag("switchUp", state.switchUpward);
  write("font", wideToUtf8(state.clockFont));
  number("fontSize", state.clockFontSize);
  flag("fontBold", state.clockFontBold);
  flag("fontItalic", state.clockFontItalic);
  write("dockFont", wideToUtf8(state.dockFont.empty() ? state.clockFont : state.dockFont));
  number("dockFontSize", state.dockFontSize);
  flag("dockFontBold", state.dockFontBold);
  flag("dockFontItalic", state.dockFontItalic);
  number("opacity", state.windowOpacity);
  flag("dock", state.dockClock);
  flag("shuffle", state.backgroundShuffle);
  flag("order", state.backgroundOrder);
  number("orderMin", state.backgroundInterval);
  write("orderAt", std::to_string(state.backgroundOrderStart));
  number("crop", state.backgroundCrop);
  flag("timeAnn", state.timeAnnounce);
  flag("hourly", state.announceMode == 0);
  number("annMode", state.announceMode);
  flag("alarm", state.alarmEnabled);
  number("alarmH", state.alarmHour);
  number("alarmM", state.alarmMinute);
  write("alarmFile", wideToUtf8(state.alarmVoice));
  number("timerH", state.timerHours);
  number("timerM", state.timerMinutes);
  number("timerS", state.timerSeconds);
  write("timerFile", wideToUtf8(state.timerVoice));
  flag("weatherAnn", state.weatherAnnounce);
  flag("quiet", state.quietMode);
  number("quietFrom", state.quietFrom);
  number("quietTo", state.quietTo);
  flag("fullSound", state.soundInFullscreen);
  number("sync", state.syncMode);
  number("cy", state.customYear);
  number("cm", state.customMonth);
  number("cd", state.customDay);
  number("ch", state.customHour);
  number("cmin", state.customMinute);
  number("cs", state.customSecond);
  flag("push", state.receivePush);
  number("dpi", state.dpiMode);
  number("theme", state.appearance);
  number("lang", state.language);
  write("image", wideToUtf8(state.backgroundImage));
  write("qhost", wideToUtf8(state.qweatherHost));
  write("qkid", wideToUtf8(state.qweatherKid));
  write("qiss", wideToUtf8(state.qweatherIssuer));
  write("qsub", wideToUtf8(state.qweatherProject));
  flag("qskip", state.qweatherSkip);
  std::ofstream keyFile{std::filesystem::path(dataPath(L"qweather_private.pem")), std::ios::binary};
  if (keyFile) keyFile << wideToUtf8(state.qweatherPrivate);
}

void prepare() {
  if (ready) return;
  ready = true;
  SYSTEMTIME now{};
  GetLocalTime(&now);
  state.customYear = now.wYear;
  state.customMonth = now.wMonth;
  state.customDay = now.wDay;
  state.customHour = now.wHour;
  state.customMinute = now.wMinute;
  state.customSecond = now.wSecond;
  loadSettings();
  std::ifstream keyInput{std::filesystem::path(dataPath(L"qweather_private.pem")), std::ios::binary};
  if (keyInput) {
    const std::string pem((std::istreambuf_iterator<char>(keyInput)), std::istreambuf_iterator<char>());
    state.qweatherPrivate = utf8ToWide(pem);
  } else {
    logMissingResource(dataPath(L"qweather_private.pem"));
  }
}

void setStartup(bool enabled) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
  if (enabled) {
    wchar_t path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length > 0) RegSetValueExW(key, L"日历", 0, REG_SZ, reinterpret_cast<const BYTE*>(path), (length + 1) * sizeof(wchar_t));
  } else {
    RegDeleteValueW(key, L"日历");
  }
  RegCloseKey(key);
}

std::wstring lunarDayText(int day) {
  const wchar_t* ones[] = {L"", L"一", L"二", L"三", L"四", L"五", L"六", L"七", L"八", L"九"};
  if (day <= 0 || day > 30) return L"";
  if (day == 10) return L"初十";
  if (day == 20) return L"二十";
  if (day == 30) return L"三十";
  if (day < 10) return std::wstring(L"初") + ones[day];
  if (day < 20) return std::wstring(L"十") + ones[day - 10];
  return std::wstring(L"廿") + ones[day - 20];
}

std::wstring chineseDigits(int value, int width) {
  std::wstring text;
  int scale = 1;
  for (int index = 1; index < width; index += 1) scale *= 10;
  for (int index = 0; index < width; index += 1) {
    text.push_back(L"〇一二三四五六七八九"[(value / scale) % 10]);
    scale /= 10;
  }
  return text;
}

bool tokenIs(const std::wstring& pattern, std::size_t index, const wchar_t* token) {
  const std::size_t length = std::wcslen(token);
  return index + length <= pattern.size() && pattern.compare(index, length, token) == 0;
}

std::wstring formatPreview(const std::wstring& pattern) {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  const LunarDate lunar = lunarFromGregorian(now.wYear, now.wMonth, now.wDay);
  const wchar_t* week[] = {L"日", L"一", L"二", L"三", L"四", L"五", L"六"};
  const wchar_t* weekEn[] = {L"Sunday", L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday"};
  const wchar_t* weekEn3[] = {L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"};
  const wchar_t* monthEn[] = {L"January", L"February", L"March", L"April", L"May", L"June", L"July", L"August", L"September", L"October", L"November", L"December"};
  const wchar_t* monthEn3[] = {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};
  const int hour12 = now.wHour % 12 == 0 ? 12 : now.wHour % 12;
  wchar_t number[16];
  std::wstring text;
  for (std::size_t index = 0; index < pattern.size();) {
    const wchar_t* tokens[] = {L"yyyy", L"YYYY", L"EM3", L"dddd", L"ddd", L"dd", L"week", L"EW3", L"yy", L"YY", L"mm", L"hh", L"HH", L"ff", L"ss", L"EM", L"EW", L"tt", L"TT", L"m", L"M", L"d", L"D", L"h", L"H", L"f", L"s"};
    const wchar_t* matched = nullptr;
    for (const wchar_t* token : tokens) {
      if (tokenIs(pattern, index, token)) {
        matched = token;
        break;
      }
    }
    if (pattern[index] == L'\\' && index + 1 < pattern.size() && pattern[index + 1] == L'n') {
      text.push_back(L'\n');
      index += 2;
      continue;
    }
    if (pattern[index] == L'"') {
      index += 1;
      while (index < pattern.size() && pattern[index] != L'"') text.push_back(pattern[index++]);
      if (index < pattern.size()) index += 1;
      continue;
    }
    if (!matched) {
      text.push_back(pattern[index]);
      index += 1;
      continue;
    }
    const std::wstring token = matched;
    if (token == L"yyyy") std::swprintf(number, 16, L"%d", now.wYear), text += number;
    else if (token == L"yy") std::swprintf(number, 16, L"%02d", now.wYear % 100), text += number;
    else if (token == L"YYYY") text += chineseDigits(now.wYear, 4) + L"年";
    else if (token == L"YY") text += chineseDigits(now.wYear % 100, 2) + L"年";
    else if (token == L"mm") std::swprintf(number, 16, L"%02d", now.wMonth), text += number;
    else if (token == L"m") std::swprintf(number, 16, L"%d", now.wMonth), text += number;
    else if (token == L"EM3") text += monthEn3[now.wMonth - 1];
    else if (token == L"EM") text += monthEn[now.wMonth - 1];
    else if (token == L"M") text += lunarMonthName(lunar);
    else if (token == L"dd") std::swprintf(number, 16, L"%02d", now.wDay), text += number;
    else if (token == L"d") std::swprintf(number, 16, L"%d", now.wDay), text += number;
    else if (token == L"D") text += lunarDayText(lunar.day);
    else if (token == L"dddd") text += std::wstring(L"星期") + week[now.wDayOfWeek];
    else if (token == L"ddd") text += std::wstring(L"周") + week[now.wDayOfWeek];
    else if (token == L"EW3") text += weekEn3[now.wDayOfWeek];
    else if (token == L"EW") text += weekEn[now.wDayOfWeek];
    else if (token == L"week") std::swprintf(number, 16, L"第%d周", dayOfYear(now.wYear, now.wMonth, now.wDay) / 7 + 1), text += number;
    else if (token == L"HH") std::swprintf(number, 16, L"%02d", now.wHour), text += number;
    else if (token == L"H") std::swprintf(number, 16, L"%d", now.wHour), text += number;
    else if (token == L"hh") std::swprintf(number, 16, L"%02d", hour12), text += number;
    else if (token == L"h") std::swprintf(number, 16, L"%d", hour12), text += number;
    else if (token == L"ff") std::swprintf(number, 16, L"%02d", now.wMinute), text += number;
    else if (token == L"f") std::swprintf(number, 16, L"%d", now.wMinute), text += number;
    else if (token == L"ss") std::swprintf(number, 16, L"%02d", now.wSecond), text += number;
    else if (token == L"s") std::swprintf(number, 16, L"%d", now.wSecond), text += number;
    else if (token == L"tt") text += now.wHour < 12 ? L"AM" : L"PM";
    else if (token == L"TT") text += now.wHour < 6 ? L"凌晨" : now.wHour < 12 ? L"上午" : now.wHour < 18 ? L"下午" : L"晚上";
    index += token.size();
  }
  return text;
}

std::wstring showNewlines(const std::wstring& pattern) {
  std::wstring text;
  for (std::size_t index = 0; index < pattern.size(); index += 1) {
    if (pattern[index] == L'\\' && index + 1 < pattern.size() && pattern[index + 1] == L'n') {
      text.push_back(L'\n');
      index += 1;
    } else {
      text.push_back(pattern[index]);
    }
  }
  return text;
}

using Gdiplus::Color;
using Gdiplus::Font;
using Gdiplus::Graphics;
using Gdiplus::Pen;
using Gdiplus::PointF;
using Gdiplus::RectF;
using Gdiplus::SolidBrush;
using Gdiplus::StringAlignment;
using Gdiplus::StringAlignmentCenter;
using Gdiplus::StringAlignmentFar;
using Gdiplus::StringAlignmentNear;
using Gdiplus::StringFormat;
using Gdiplus::UnitPixel;

Color colorOf(BYTE red, BYTE green, BYTE blue) {
  return Color(255, red, green, blue);
}

void drawText(Graphics& graphics, const wchar_t* text, const Font& font, const RectF& bounds, Color color, StringAlignment align, bool wrap = false) {
  SolidBrush brush(color);
  StringFormat format;
  format.SetAlignment(align);
  format.SetLineAlignment(wrap ? Gdiplus::StringAlignmentNear : StringAlignmentCenter);
  if (!wrap) format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
  format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
  graphics.DrawString(text, -1, &font, bounds, &format, &brush);
}

void addHit(int id, const RectF& rect) {
  if (rect.Width < 1.f || rect.Height < 1.f) return;
  if (rect.Y + rect.Height < 0.f) return;
  hits.push_back(Hit{id, rect});
}

void drawToggle(Graphics& graphics, const RectF& rect, bool on) {
  const float height = rect.Height;
  SolidBrush track(on ? colorOf(245, 104, 104) : colorOf(186, 186, 186));
  graphics.FillEllipse(&track, rect.X, rect.Y, height, height);
  graphics.FillEllipse(&track, rect.X + rect.Width - height, rect.Y, height, height);
  graphics.FillRectangle(&track, rect.X + height / 2.f, rect.Y, rect.Width - height, height);
  SolidBrush knob(colorOf(255, 255, 255));
  const float knobX = on ? rect.X + rect.Width - height + dp(2) : rect.X + dp(2);
  graphics.FillEllipse(&knob, knobX, rect.Y + dp(2), height - dp(4), height - dp(4));
}

void drawRadio(Graphics& graphics, float x, float y, bool on, bool enabled) {
  Pen pen(enabled ? colorOf(120, 120, 120) : colorOf(190, 190, 190), 1.4f);
  const float size = dp(16);
  graphics.DrawEllipse(&pen, x, y, size, size);
  if (!on) return;
  SolidBrush mark(enabled ? colorOf(80, 80, 80) : colorOf(190, 190, 190));
  graphics.FillEllipse(&mark, x + dp(4), y + dp(4), size - dp(8), size - dp(8));
}

void drawCheck(Graphics& graphics, const RectF& rect, bool on) {
  Pen pen(colorOf(120, 120, 120), 1.3f);
  graphics.DrawRectangle(&pen, rect);
  if (!on) return;
  Pen mark(colorOf(60, 60, 60), 1.8f);
  graphics.DrawLine(&mark, rect.X + dp(3), rect.Y + rect.Height * 0.55f, rect.X + rect.Width * 0.42f, rect.Y + rect.Height - dp(3));
  graphics.DrawLine(&mark, rect.X + rect.Width * 0.42f, rect.Y + rect.Height - dp(3), rect.X + rect.Width - dp(3), rect.Y + dp(3));
}

void drawChevron(Graphics& graphics, float x, float y, Color color) {
  Pen pen(color, 1.4f);
  graphics.DrawLine(&pen, x, y, x + dp(5), y + dp(4));
  graphics.DrawLine(&pen, x + dp(5), y + dp(4), x + dp(10), y);
}

RectF drawOption(Graphics& graphics, const Font& font, float x, float y, const wchar_t* text, bool on, bool enabled) {
  drawRadio(graphics, x, y + dp(4), on, enabled);
  const Color color = enabled ? colorOf(60, 60, 60) : colorOf(176, 176, 176);
  const float width = dp(16) * static_cast<float>(std::wcslen(text)) + dp(8);
  drawText(graphics, text, font, RectF(x + dp(22), y, width, dp(26)), color, StringAlignmentNear);
  return RectF(x, y, dp(22) + width, dp(28));
}

void drawField(Graphics& graphics, const RectF& rect, const wchar_t* text, bool enabled) {
  SolidBrush fill(colorOf(255, 255, 255));
  graphics.FillRectangle(&fill, rect);
  Pen border(enabled ? colorOf(214, 214, 214) : colorOf(226, 226, 226), 1.f);
  graphics.DrawRectangle(&border, rect);
  if (text && text[0]) drawText(graphics, text, Font(L"Microsoft YaHei", dp(13), Gdiplus::FontStyleRegular, UnitPixel), RectF(rect.X + dp(8), rect.Y, rect.Width - dp(28), rect.Height), enabled ? colorOf(60, 60, 60) : colorOf(176, 176, 176), StringAlignmentNear);
  drawChevron(graphics, rect.X + rect.Width - dp(18), rect.Y + rect.Height / 2.f - dp(2), enabled ? colorOf(120, 120, 120) : colorOf(190, 190, 190));
}

HICON programIcon = nullptr;

void drawLogo(Graphics& graphics) {
  const float x = dp(16);
  const float y = dp(16);
  const int pixels = std::max(16, static_cast<int>(dp(36)));
  if (!programIcon) {
    programIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, pixels, pixels, LR_DEFAULTCOLOR));
  }
  if (programIcon) {
    HDC hdc = graphics.GetHDC();
    DrawIconEx(hdc, static_cast<int>(x), static_cast<int>(y), programIcon, pixels, pixels, 0, nullptr, DI_NORMAL);
    graphics.ReleaseHDC(hdc);
  }
  Font font(L"Microsoft YaHei", dp(18), Gdiplus::FontStyleBold, UnitPixel);
  drawText(graphics, S(L"蕊历", L"Calendar"), font, RectF(x + static_cast<float>(pixels) + dp(8), y, dp(88), static_cast<float>(pixels)), colorOf(45, 45, 45), StringAlignmentNear);
}

std::vector<std::wstring> menuItems(int menu) {
  std::vector<std::wstring> items;
  wchar_t text[16];
  const bool english = state.language == 1;
  if (menu == kMenuHour || menu == kMenuQuietFrom || menu == kMenuQuietTo) {
    const wchar_t* suffix = menu == kMenuHour ? (english ? L"" : L"时") : (english ? L"" : L"点");
    for (int index = 0; index < 24; index += 1) {
      if (english) std::swprintf(text, 16, L"%02d", index);
      else std::swprintf(text, 16, L"%d%ls", index, suffix);
      items.emplace_back(text);
    }
  } else if (menu == kMenuMinute || menu == kMenuSecond) {
    const wchar_t* suffix = menu == kMenuMinute ? L"分" : L"秒";
    for (int index = 0; index < 60; index += 1) {
      if (english) std::swprintf(text, 16, L"%02d", index);
      else std::swprintf(text, 16, L"%d%ls", index, suffix);
      items.emplace_back(text);
    }
  } else if (menu == kMenuSync) {
    items = english ? std::vector<std::wstring>{L"Off", L"Hourly", L"Daily", L"Weekly"} : std::vector<std::wstring>{L"无", L"每小时", L"每天", L"每周"};
  } else if (menu == kMenuDpi) {
    items = english ? std::vector<std::wstring>{L"System", L"100%", L"125%", L"150%", L"200%"} : std::vector<std::wstring>{L"跟随系统", L"100%", L"125%", L"150%", L"200%"};
  } else if (menu == kMenuLanguage) {
    items = {L"中文", L"English"};
  } else if (menu == kMenuInterval) {
    const int minutes[] = {1, 5, 10, 30, 60, 180, 480};
    for (int minute : minutes) {
      if (english) std::swprintf(text, 16, L"%d min", minute);
      else std::swprintf(text, 16, L"%d 分钟", minute);
      items.emplace_back(text);
    }
  }
  return items;
}

int menuSelected(int menu) {
  if (menu == kMenuHour) return state.customHour;
  if (menu == kMenuMinute) return state.customMinute;
  if (menu == kMenuSecond) return state.customSecond;
  if (menu == kMenuQuietFrom) return state.quietFrom;
  if (menu == kMenuQuietTo) return state.quietTo;
  if (menu == kMenuSync) return state.syncMode;
  if (menu == kMenuDpi) return state.dpiMode;
  if (menu == kMenuLanguage) return state.language == 1 ? 1 : 0;
  if (menu == kMenuInterval) {
    const int minutes[] = {1, 5, 10, 30, 60, 180, 480};
    const int chosen = backgroundIntervalMinutes(state.backgroundInterval);
    for (int index = 0; index < 7; index += 1) {
      if (minutes[index] == chosen) return index;
    }
  }
  return 0;
}

std::wstring menuLabel(int menu) {
  const std::vector<std::wstring> items = menuItems(menu);
  const int selected = menuSelected(menu);
  if (selected < 0 || selected >= static_cast<int>(items.size())) return L"";
  return items[static_cast<std::size_t>(selected)];
}

void drawMenu(Graphics& graphics, const Font& font) {
  if (openMenu < kMenuSync) return;
  const std::vector<std::wstring> items = menuItems(openMenu);
  const float row = dp(26);
  const int visible = 8;
  const int maxScroll = std::max(0, static_cast<int>(items.size()) - visible);
  menuScroll = std::max(0, std::min(menuScroll, maxScroll));
  RectF list(menuAnchor.X, menuAnchor.Y + menuAnchor.Height, std::max(menuAnchor.Width, dp(108)), row * visible);
  SolidBrush fill(colorOf(255, 255, 255));
  graphics.FillRectangle(&fill, list);
  Pen border(colorOf(210, 210, 210), 1.f);
  graphics.DrawRectangle(&border, list);
  addHit(kMenuBlock, list);
  const int selected = menuSelected(openMenu);
  for (int index = 0; index < visible && menuScroll + index < static_cast<int>(items.size()); index += 1) {
    const int item = menuScroll + index;
    RectF rowRect(list.X + 1.f, list.Y + index * row, list.Width - 2.f, row);
    if (item == selected) {
      SolidBrush mark(colorOf(232, 240, 250));
      graphics.FillRectangle(&mark, rowRect);
    }
    drawText(graphics, items[static_cast<std::size_t>(item)].c_str(), font, RectF(rowRect.X + dp(8), rowRect.Y, rowRect.Width - dp(12), rowRect.Height), colorOf(55, 55, 55), StringAlignmentNear);
    addHit(kMenuItem + item, rowRect);
  }
}

void drawFormatPanel(Graphics& graphics, const Font& font, float left, float top, float width) {
  const float height = dp(318);
  RectF panel(left, top, width, height);
  SolidBrush fill(colorOf(252, 252, 252));
  graphics.FillRectangle(&fill, panel);
  Pen border(colorOf(206, 206, 206), 1.f);
  graphics.DrawRectangle(&border, panel);
  addHit(kMenuBlock, panel);
  formatPlace = EditPlace{true, static_cast<int>(left + dp(10)), static_cast<int>(top + dp(10)), static_cast<int>(width - dp(20)), static_cast<int>(dp(28))};
  const std::wstring preview = formatPreview(openMenu == kMenuFormat1 ? state.formatPrimary : state.formatSecondary);
  Font small(L"Microsoft YaHei", dp(12), Gdiplus::FontStyleRegular, UnitPixel);
  drawText(graphics, L"预览:", small, RectF(left + dp(12), top + dp(46), dp(48), dp(22)), colorOf(70, 130, 190), StringAlignmentNear);
  drawText(graphics, preview.c_str(), small, RectF(left + dp(64), top + dp(44), width - dp(80), dp(40)), colorOf(47, 112, 186), StringAlignmentNear, true);
  if (openMenu != kMenuFormat1) return;
  const wchar_t* leftLines[] = {
      L"时间变量",
      L"年: yy→26   yyyy→2026",
      L"YY→二六年   YYYY→二〇二六年",
      L"月: m→11 mm→11  M→九月",
      L"EM3→Nov   EM→November",
      L"日: d→7 dd→07   D→廿九",
      L"时: h→3 hh→03   H→3 HH→03",
      L"分: f→51 ff→51    秒: s→9 ss→09",
      L"星期: ddd→周六  dddd→星期一",
      L"EW3→Sat  EW→Saturday  week→第45周",
      L"上下午: tt→AM   TT→凌晨",
      L"字符: \\n→换行   \"文本\"用引号",
  };
  const wchar_t* rightLines[] = {
      L"天气变量",
      L"6日天气: we1→晴  we2→多云",
      L"6日温度: tl1→13℃  th1→21℃",
      L"实时温度: rtm→14℃",
      L"实时湿度: rhu→84%",
      L"风力风向: rwd→东北风",
      L"地理位置: pos→平湖",
      L"天气图标: icn→图标",
      L"空气质量: aqi→优",
  };
  float lineY = top + dp(92);
  for (const wchar_t* line : leftLines) {
    drawText(graphics, line, small, RectF(left + dp(12), lineY, width * 0.56f, dp(18)), colorOf(55, 110, 175), StringAlignmentNear);
    lineY += dp(17);
  }
  lineY = top + dp(92);
  for (const wchar_t* line : rightLines) {
    drawText(graphics, line, small, RectF(left + width * 0.56f, lineY, width * 0.42f, dp(18)), colorOf(55, 110, 175), StringAlignmentNear);
    lineY += dp(17);
  }
}

void paintSettings(Graphics& graphics, int width, int height) {
  hits.clear();
  hoverPlace = {};
  switch1Place = {};
  switch2Place = {};
  formatPlace = {};
  datePlace = {};
  graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
  SolidBrush sidebar(colorOf(188, 188, 188));
  SolidBrush content(colorOf(236, 236, 236));
  graphics.FillRectangle(&content, 0, 0, width, height);
  const float side = dp(kSideWidth);
  graphics.FillRectangle(&sidebar, 0.f, 0.f, side, static_cast<float>(height));
  drawLogo(graphics);
  Font nav(L"Microsoft YaHei", dp(15), Gdiplus::FontStyleRegular, UnitPixel);
  Font navBold(L"Microsoft YaHei", dp(15), Gdiplus::FontStyleBold, UnitPixel);
  const wchar_t* pages[] = {S(L"功能设置", L"General"), S(L"显示设置", L"Display"), S(L"语音设置", L"Voice"), S(L"外观设置", L"Look"), S(L"高级设置", L"Advanced"), S(L"关于", L"About")};
  for (int index = 0; index < 6; index += 1) {
    const float y = dp(84) + index * dp(42);
    if (page == index) {
      SolidBrush bar(colorOf(226, 68, 68));
      graphics.FillRectangle(&bar, 0, static_cast<int>(y + dp(4)), static_cast<int>(dp(4)), static_cast<int>(dp(28)));
    }
    drawText(graphics, pages[index], page == index ? navBold : nav, RectF(dp(22), y, side - dp(28), dp(36)), colorOf(55, 55, 55), StringAlignmentNear);
    addHit(kPageBase + index, RectF(0, y, side, dp(40)));
  }

  Font body(L"Microsoft YaHei", dp(14), Gdiplus::FontStyleRegular, UnitPixel);
  Font title(L"Microsoft YaHei", dp(16), Gdiplus::FontStyleBold, UnitPixel);
  Font small(L"Microsoft YaHei", dp(13), Gdiplus::FontStyleRegular, UnitPixel);
  const float labelX = dp(174);
  const float labelW = dp(96);
  const float controlX = dp(278);
  const float contentRight = static_cast<float>(width) - dp(16);
  const float span = std::max(dp(280), contentRight - controlX);
  float y = dp(16) - static_cast<float>(scroll);
  auto heading = [&](const wchar_t* text) {
    drawText(graphics, text, title, RectF(labelX + dp(8), y, dp(280), dp(28)), colorOf(40, 40, 40), StringAlignmentNear);
    y += dp(34);
  };
  auto label = [&](const wchar_t* text, float rowY) {
    drawText(graphics, text, body, RectF(labelX, rowY, labelW, dp(28)), colorOf(70, 70, 70), StringAlignmentFar);
  };
  auto toggleAt = [&](int id, bool on, float rowY) {
    RectF rect(controlX, rowY + dp(2), dp(46), dp(24));
    drawToggle(graphics, rect, on);
    addHit(id, rect);
  };

  if (page == 0) {
    heading(S(L"基本设置", L"Basics"));
    label(S(L"开机加载:", L"Startup:"), y);
    toggleAt(kLaunch, state.launchAtStartup, y);
    y += dp(38);
    label(S(L"24时制式:", L"24-hour:"), y);
    toggleAt(kHour24, state.hour24, y);
    y += dp(38);
    label(S(L"属相年:", L"Zodiac:"), y);
    RectF lichun = drawOption(graphics, body, controlX, y, S(L"从立春开始", L"From Start of Spring"), state.zodiacFromLichun, true);
    RectF chuyi = drawOption(graphics, body, controlX + dp(156), y, S(L"从初一开始", L"From lunar new year"), !state.zodiacFromLichun, true);
    addHit(kZodiacLichun, lichun);
    addHit(kZodiacChuyi, chuyi);
    y += dp(38);
    label(S(L"每周首日:", L"Week start:"), y);
    RectF sunday = drawOption(graphics, body, controlX, y, S(L"从周日开始", L"Sunday"), !state.weekStartsMonday, true);
    RectF monday = drawOption(graphics, body, controlX + dp(156), y, S(L"从周一开始", L"Monday"), state.weekStartsMonday, true);
    addHit(kWeekSunday, sunday);
    addHit(kWeekMonday, monday);
    y += dp(42);
    heading(S(L"悬浮窗设置", L"Popup"));
    RectF check(controlX, y + dp(4), dp(16), dp(16));
    drawCheck(graphics, check, state.showWeatherFloat);
    addHit(kFloat, RectF(check.X, y, dp(148), dp(26)));
    drawText(graphics, S(L"显示天气悬浮窗口", L"Weather popup"), body, RectF(controlX + dp(24), y, dp(132), dp(26)), colorOf(60, 60, 60), StringAlignmentNear);
    drawText(graphics, S(L"鼠标悬浮提示框显示间隔:", L"Hover delay:"), small, RectF(controlX + dp(158), y, dp(176), dp(26)), colorOf(60, 60, 60), StringAlignmentNear);
    RectF hover(controlX + dp(336), y, dp(48), dp(28));
    SolidBrush white(colorOf(255, 255, 255));
    graphics.FillRectangle(&white, hover);
    Pen border(colorOf(214, 214, 214), 1.f);
    graphics.DrawRectangle(&border, hover);
    hoverPlace = {true, static_cast<int>(hover.X + dp(4)), static_cast<int>(hover.Y + dp(3)), static_cast<int>(hover.Width - dp(8)), static_cast<int>(hover.Height - dp(6))};
    drawText(graphics, S(L"秒", L"s"), body, RectF(hover.X + hover.Width + dp(8), y, dp(28), dp(28)), colorOf(60, 60, 60), StringAlignmentNear);
    y += dp(42);
    heading(S(L"弹窗显示", L"Alerts"));
    label(S(L"天气提醒:", L"Weather:"), y);
    toggleAt(kAlert, state.weatherAlert, y);
    y += dp(36);
  } else if (page == 1) {
    heading(S(L"任务栏时间区域", L"Taskbar clock"));
    label(S(L"时钟字体:", L"Clock font:"), y);
    drawText(graphics, S(L"设置字体", L"Choose font"), body, RectF(controlX, y, dp(90), dp(28)), colorOf(214, 52, 52), StringAlignmentNear);
    addHit(kFont, RectF(controlX, y, dp(90), dp(28)));
    drawText(graphics, fontSummary(state.dockFont, state.dockFontSize, state.dockFontBold, state.dockFontItalic).c_str(), small, RectF(controlX + dp(96), y, span - dp(96), dp(28)), colorOf(90, 90, 90), StringAlignmentNear);
    y += dp(38);
    label(S(L"对齐方式:", L"Align:"), y);
    addHit(kAlignLeft, drawOption(graphics, body, controlX, y, S(L"居左", L"Left"), state.clockAlign == 0, true));
    addHit(kAlignCenter, drawOption(graphics, body, controlX + dp(100), y, S(L"居中", L"Center"), state.clockAlign == 1, true));
    addHit(kAlignRight, drawOption(graphics, body, controlX + dp(200), y, S(L"居右", L"Right"), state.clockAlign == 2, true));
    y += dp(38);
    label(S(L"显示模式:", L"Mode:"), y);
    addHit(kModeSingle, drawOption(graphics, body, controlX, y, S(L"仅显示第一屏", L"First screen"), !state.dualScreen, true));
    addHit(kModeDual, drawOption(graphics, body, controlX + dp(168), y, S(L"双屏滚动显示", L"Two screens"), state.dualScreen, true));
    y += dp(36);
    drawText(graphics, S(L"第一屏格式", L"Screen 1"), small, RectF(controlX, y, dp(140), dp(22)), colorOf(70, 70, 70), StringAlignmentNear);
    y += dp(22);
    const float boxW = std::max(dp(220), span - dp(96));
    RectF primary(controlX, y, boxW, dp(46));
    SolidBrush white(colorOf(255, 255, 255));
    graphics.FillRectangle(&white, primary);
    Pen border(colorOf(214, 214, 214), 1.f);
    graphics.DrawRectangle(&border, primary);
    const std::wstring primaryPreview = formatPreview(state.formatPrimary);
    drawText(graphics, primaryPreview.c_str(), small, RectF(primary.X + dp(10), primary.Y + dp(6), primary.Width - dp(36), primary.Height - dp(8)), colorOf(55, 55, 55), StringAlignmentNear, true);
    drawChevron(graphics, primary.X + primary.Width - dp(20), primary.Y + primary.Height / 2.f, colorOf(214, 64, 64));
    addHit(kFormatBox, primary);
    drawText(graphics, S(L"格式解释", L"Format help"), body, RectF(primary.X + primary.Width + dp(8), y + dp(8), dp(84), dp(28)), colorOf(214, 52, 52), StringAlignmentNear);
    addHit(kFormatHelp, RectF(primary.X + primary.Width + dp(8), y, dp(88), dp(40)));
    const float panelTop = primary.Y + primary.Height + dp(2);
    y += dp(54);
    drawText(graphics, S(L"第二屏格式", L"Screen 2"), small, RectF(controlX, y, dp(140), dp(22)), colorOf(70, 70, 70), StringAlignmentNear);
    y += dp(22);
    RectF secondary(controlX, y, boxW, dp(46));
    graphics.FillRectangle(&white, secondary);
    graphics.DrawRectangle(&border, secondary);
    drawText(graphics, showNewlines(state.formatSecondary).c_str(), small, RectF(secondary.X + dp(10), secondary.Y + dp(6), secondary.Width - dp(36), secondary.Height - dp(8)), colorOf(55, 55, 55), StringAlignmentNear, true);
    drawChevron(graphics, secondary.X + secondary.Width - dp(20), secondary.Y + secondary.Height / 2.f, colorOf(120, 120, 120));
    addHit(kFormat2Box, secondary);
    y += dp(54);
    drawText(graphics, S(L"注意：任务栏竖直放置时，如果宽度较小显示不开时，会使用固定样式。", L"A narrow vertical taskbar uses a fixed layout."), small, RectF(controlX, y, span, dp(36)), colorOf(214, 52, 52), StringAlignmentNear, true);
    y += dp(38);
    RectF dockCheck(controlX, y + dp(4), dp(16), dp(16));
    drawCheck(graphics, dockCheck, state.dockClock);
    addHit(kDock, RectF(dockCheck.X, y, span, dp(26)));
    drawText(graphics, S(L"占据系统时钟位置，显示上面的第一屏格式", L"Cover the clock with screen 1"), body, RectF(controlX + dp(24), y, span - dp(24), dp(26)), colorOf(60, 60, 60), StringAlignmentNear);
    y += dp(36);
    heading(S(L"任务栏时间天气切换", L"Clock and weather"));
    drawText(graphics, S(L"第一屏切换间隔", L"Screen 1"), small, RectF(controlX, y, dp(110), dp(28)), colorOf(60, 60, 60), StringAlignmentNear);
    RectF firstGap(controlX + dp(112), y, dp(42), dp(28));
    graphics.FillRectangle(&white, firstGap);
    graphics.DrawRectangle(&border, firstGap);
    switch1Place = {openMenu < kMenuFormat1, static_cast<int>(firstGap.X + dp(2)), static_cast<int>(firstGap.Y + dp(3)), static_cast<int>(firstGap.Width - dp(4)), static_cast<int>(firstGap.Height - dp(6))};
    drawText(graphics, S(L"秒", L"s"), small, RectF(firstGap.X + firstGap.Width + dp(6), y, dp(24), dp(28)), colorOf(60, 60, 60), StringAlignmentNear);
    drawText(graphics, S(L"第二屏切换间隔", L"Screen 2"), small, RectF(controlX + dp(190), y, dp(110), dp(28)), colorOf(60, 60, 60), StringAlignmentNear);
    RectF secondGap(controlX + dp(304), y, dp(42), dp(28));
    graphics.FillRectangle(&white, secondGap);
    graphics.DrawRectangle(&border, secondGap);
    switch2Place = {openMenu < kMenuFormat1, static_cast<int>(secondGap.X + dp(2)), static_cast<int>(secondGap.Y + dp(3)), static_cast<int>(secondGap.Width - dp(4)), static_cast<int>(secondGap.Height - dp(6))};
    drawText(graphics, S(L"秒", L"s"), small, RectF(secondGap.X + secondGap.Width + dp(6), y, dp(24), dp(28)), colorOf(60, 60, 60), StringAlignmentNear);
    y += dp(36);
    addHit(kSwitchUp, drawOption(graphics, body, controlX, y, S(L"从下向上切换", L"Up"), state.switchUpward, true));
    addHit(kSwitchSide, drawOption(graphics, body, controlX + dp(168), y, S(L"从右向左切换", L"Left"), !state.switchUpward, true));
    y += dp(36);
    if (openMenu == kMenuFormat1 || openMenu == kMenuFormat2) {
      const float anchor = openMenu == kMenuFormat1 ? panelTop : secondary.Y + secondary.Height + dp(2);
      drawFormatPanel(graphics, body, controlX, anchor, span);
    }
  } else if (page == 2) {
    heading(S(L"语音提示", L"Voice"));
    label(S(L"时间播报:", L"Clock:"), y);
    toggleAt(kTimeAnn, state.timeAnnounce, y);
    addHit(kHourly, drawOption(graphics, body, controlX + dp(64), y, S(L"整点报时", L"On the hour"), state.announceMode == 0, state.timeAnnounce));
    addHit(kHalf, drawOption(graphics, body, controlX + dp(210), y, S(L"半点报时", L"Half hour"), state.announceMode == 1, state.timeAnnounce));
    y += dp(36);
    addHit(kTen, drawOption(graphics, body, controlX + dp(64), y, S(L"每十分钟报时", L"Every 10 min"), state.announceMode == 2, state.timeAnnounce));
    addHit(kMinuteAnn, drawOption(graphics, body, controlX + dp(210), y, S(L"每分钟报时", L"Every minute"), state.announceMode == 3, state.timeAnnounce));
    y += dp(40);
    label(S(L"天气播报:", L"Weather:"), y);
    toggleAt(kWeatherAnn, state.weatherAnnounce, y);
    y += dp(40);
    label(S(L"勿扰模式:", L"Quiet:"), y);
    toggleAt(kQuiet, state.quietMode, y);
    RectF fromBox(controlX + dp(64), y, dp(86), dp(30));
    RectF toBox(controlX + dp(196), y, dp(86), dp(30));
    drawField(graphics, fromBox, menuLabel(kMenuQuietFrom).c_str(), state.quietMode);
    drawText(graphics, S(L"至", L"to"), body, RectF(fromBox.X + fromBox.Width + dp(8), y, dp(28), dp(30)), state.quietMode ? colorOf(60, 60, 60) : colorOf(176, 176, 176), StringAlignmentCenter);
    drawField(graphics, toBox, menuLabel(kMenuQuietTo).c_str(), state.quietMode);
    if (state.quietMode) {
      addHit(kMenuQuietFrom, fromBox);
      addHit(kMenuQuietTo, toBox);
    }
    y += dp(44);
    heading(S(L"声音播报", L"Sound"));
    RectF check(controlX, y + dp(4), dp(16), dp(16));
    drawCheck(graphics, check, state.soundInFullscreen);
    addHit(kFullSound, RectF(check.X, y, span, dp(26)));
    drawText(graphics, S(L"全屏程序时仍播报声音", L"Speak during fullscreen"), body, RectF(controlX + dp(24), y, span - dp(24), dp(26)), colorOf(60, 60, 60), StringAlignmentNear);
    y += dp(34);
  } else if (page == 4) {
    heading(S(L"时间设置", L"Time"));
    label(S(L"网络对时:", L"Sync now:"), y);
    drawText(graphics, S(L"立即对时", L"Sync"), body, RectF(controlX, y, dp(90), dp(28)), colorOf(214, 52, 52), StringAlignmentNear);
    addHit(kSyncNow, RectF(controlX, y, dp(90), dp(28)));
    y += dp(38);
    label(S(L"自动同步时间:", L"Auto sync:"), y);
    RectF syncBox(controlX, y, dp(150), dp(32));
    drawField(graphics, syncBox, menuLabel(kMenuSync).c_str(), true);
    addHit(kMenuSync, syncBox);
    y += dp(42);
    label(S(L"自定义时间:", L"Custom:"), y);
    RectF dateBox(controlX, y, dp(108), dp(32));
    SolidBrush white(colorOf(255, 255, 255));
    graphics.FillRectangle(&white, dateBox);
    Pen border(colorOf(214, 214, 214), 1.f);
    graphics.DrawRectangle(&border, dateBox);
    datePlace = {true, static_cast<int>(dateBox.X + dp(6)), static_cast<int>(dateBox.Y + dp(4)), static_cast<int>(dateBox.Width - dp(28)), static_cast<int>(dateBox.Height - dp(8))};
    drawChevron(graphics, dateBox.X + dateBox.Width - dp(18), dateBox.Y + dp(12), colorOf(120, 120, 120));
    RectF hourBox(controlX + dp(116), y, dp(64), dp(32));
    RectF minuteBox(controlX + dp(186), y, dp(64), dp(32));
    RectF secondBox(controlX + dp(256), y, dp(64), dp(32));
    drawField(graphics, hourBox, menuLabel(kMenuHour).c_str(), true);
    drawField(graphics, minuteBox, menuLabel(kMenuMinute).c_str(), true);
    drawField(graphics, secondBox, menuLabel(kMenuSecond).c_str(), true);
    addHit(kMenuHour, hourBox);
    addHit(kMenuMinute, minuteBox);
    addHit(kMenuSecond, secondBox);
    drawText(graphics, S(L"立即设置时间", L"Save time"), body, RectF(secondBox.X + secondBox.Width + dp(8), y, dp(108), dp(32)), colorOf(214, 52, 52), StringAlignmentNear);
    addHit(kSetTime, RectF(secondBox.X + secondBox.Width + dp(8), y, dp(108), dp(32)));
    y += dp(32);
    if (!statusText.empty()) drawText(graphics, statusText.c_str(), small, RectF(controlX, y, span, dp(22)), colorOf(214, 52, 52), StringAlignmentNear);
    y += dp(28);
    heading(S(L"推送设置", L"Push"));
    label(S(L"接收推送消息:", L"Receive:"), y);
    toggleAt(kPush, state.receivePush, y);
    y += dp(42);
    heading(S(L"适配屏幕DPI", L"Screen DPI"));
    RectF dpiBox(controlX, y, dp(180), dp(34));
    drawField(graphics, dpiBox, menuLabel(kMenuDpi).c_str(), true);
    addHit(kMenuDpi, dpiBox);
    y += dp(44);
    label(S(L"天气来源:", L"Weather:"), y);
    RectF qweather(controlX, y, dp(150), dp(32));
    SolidBrush qweatherFill(colorOf(214, 52, 52));
    graphics.FillRectangle(&qweatherFill, qweather);
    drawText(graphics, S(L"和风天气账号", L"QWeather"), body, qweather, colorOf(255, 255, 255), StringAlignmentCenter);
    addHit(kQWeather, qweather);
    y += dp(36);
  } else if (page == 3) {
    heading(S(L"界面配色", L"Appearance"));
    label(S(L"语言:", L"Language:"), y);
    RectF languageBox(controlX, y, dp(160), dp(32));
    drawField(graphics, languageBox, menuLabel(kMenuLanguage).c_str(), true);
    addHit(kMenuLanguage, languageBox);
    y += dp(38);
    label(S(L"外观:", L"Theme:"), y);
    RectF night = drawOption(graphics, body, controlX, y, S(L"夜间", L"Night"), state.appearance == 0, true);
    RectF day = drawOption(graphics, body, controlX + dp(110), y, S(L"白天", L"Day"), state.appearance == 1, true);
    addHit(kThemeNight, night);
    addHit(kThemeDay, day);
    y += dp(36);
    RectF image = drawOption(graphics, body, controlX, y, S(L"自定义背景", L"Picture"), state.appearance == 2, true);
    addHit(kThemeImage, image);
    y += dp(36);
    label(S(L"背景:", L"Background:"), y);
    const wchar_t* imageName = state.backgroundImage.empty() ? S(L"未选择", L"None") : state.backgroundImage.c_str();
    drawText(graphics, imageName, small, RectF(controlX, y, span, dp(28)), colorOf(90, 90, 90), StringAlignmentNear);
    y += dp(32);
    RectF pick(controlX, y, dp(132), dp(32));
    SolidBrush pickFill(colorOf(214, 52, 52));
    graphics.FillRectangle(&pickFill, pick);
    drawText(graphics, S(L"选择文件", L"Choose file"), body, pick, colorOf(255, 255, 255), StringAlignmentCenter);
    addHit(kPickImage, pick);
    drawText(graphics, S(L"视频不超过 20 MB", L"Video up to 20 MB"), small, RectF(controlX + dp(144), y, span - dp(144), dp(32)), colorOf(120, 120, 120), StringAlignmentNear);
    y += dp(38);
    label(S(L"播放:", L"Play:"), y);
    RectF shuffleToggle(controlX, y + dp(2), dp(46), dp(24));
    drawToggle(graphics, shuffleToggle, state.backgroundShuffle);
    addHit(kShuffle, shuffleToggle);
    drawText(graphics, S(L"随机", L"Shuffle"), body, RectF(controlX + dp(52), y, dp(52), dp(28)), colorOf(60, 60, 60), StringAlignmentNear);
    RectF orderToggle(controlX + dp(112), y + dp(2), dp(46), dp(24));
    drawToggle(graphics, orderToggle, state.backgroundOrder);
    addHit(kOrder, orderToggle);
    drawText(graphics, S(L"顺序", L"Order"), body, RectF(controlX + dp(164), y, dp(52), dp(28)), colorOf(60, 60, 60), StringAlignmentNear);
    y += dp(38);
    label(S(L"切换间隔:", L"Interval:"), y);
    RectF intervalBox(controlX, y, dp(128), dp(32));
    drawField(graphics, intervalBox, menuLabel(kMenuInterval).c_str(), true);
    addHit(kMenuInterval, intervalBox);
    drawText(graphics, S(L"从点击确认开始", L"Starts at confirm"), small, RectF(intervalBox.X + intervalBox.Width + dp(12), y, dp(180), dp(32)), colorOf(120, 120, 120), StringAlignmentNear);
    y += dp(38);
    label(S(L"裁切:", L"Crop:"), y);
    addHit(kCropLeft, drawOption(graphics, body, controlX, y, S(L"居左", L"Left"), state.backgroundCrop == 0, true));
    addHit(kCropCenter, drawOption(graphics, body, controlX + dp(100), y, S(L"居中", L"Center"), state.backgroundCrop == 1, true));
    addHit(kCropRight, drawOption(graphics, body, controlX + dp(200), y, S(L"居右", L"Right"), state.backgroundCrop == 2, true));
    y += dp(38);
    label(S(L"月历字体:", L"Calendar font:"), y);
    drawText(graphics, S(L"设置字体", L"Choose font"), body, RectF(controlX, y, dp(90), dp(28)), colorOf(214, 52, 52), StringAlignmentNear);
    addHit(kCalendarFont, RectF(controlX, y, dp(90), dp(28)));
    drawText(graphics, fontSummary(state.clockFont, state.clockFontSize, state.clockFontBold, state.clockFontItalic).c_str(), small, RectF(controlX + dp(96), y, span - dp(96), dp(28)), colorOf(90, 90, 90), StringAlignmentNear);
    y += dp(38);
    label(S(L"窗口透明度:", L"Opacity:"), y);
    opacityTrack = RectF(controlX, y + dp(12), dp(220), dp(6));
    SolidBrush trackBrush(colorOf(214, 214, 214));
    graphics.FillRectangle(&trackBrush, opacityTrack);
    const float portion = (std::max(40, std::min(100, state.windowOpacity)) - 40) / 60.f;
    SolidBrush filled(colorOf(214, 52, 52));
    graphics.FillRectangle(&filled, opacityTrack.X, opacityTrack.Y, opacityTrack.Width * portion, opacityTrack.Height);
    const float knob = opacityTrack.X + opacityTrack.Width * portion;
    graphics.FillEllipse(&filled, knob - dp(7), opacityTrack.Y - dp(5), dp(16), dp(16));
    addHit(kOpacity, RectF(opacityTrack.X - dp(8), y, opacityTrack.Width + dp(16), dp(32)));
    wchar_t opacityText[16];
    std::swprintf(opacityText, 16, L"%d%%", state.windowOpacity);
    drawText(graphics, opacityText, body, RectF(opacityTrack.X + opacityTrack.Width + dp(16), y, dp(60), dp(28)), colorOf(60, 60, 60), StringAlignmentNear);
    y += dp(36);
  } else if (page == 5) {
    heading(S(L"关于", L"About"));
    drawText(graphics, S(L"蕊历", L"Calendar"), title, RectF(controlX, y, span, dp(26)), colorOf(40, 40, 40), StringAlignmentNear);
    y += dp(30);
    drawText(graphics, S(L"作者与版权", L"Author and copyright"), body, RectF(controlX, y, span, dp(22)), colorOf(70, 70, 70), StringAlignmentNear);
    y += dp(24);
    const wchar_t* copyright = S(L"版权所有 © 2026。本程序在本机使用，不另署个人作者姓名。", L"Copyright © 2026. This local calendar does not name a separate author.");
    const wchar_t* terms = S(L"供本机个人使用。天气、城市和历史上的今天来自对应的公开接口，以对方的条款为准。闹钟和计时只在本程序运行时生效。程序按现状提供，不对提醒是否被听到作出保证。", L"For personal use on this computer. Weather, places and On This Day come from public services under their own terms. Alarms and timers work only while this program is running. It is provided as it is, with no promise that a reminder will be heard.");
    Gdiplus::StringFormat logFormat;
    logFormat.SetAlignment(Gdiplus::StringAlignmentNear);
    auto blockHeight = [&](const wchar_t* text) {
      Gdiplus::RectF layout(controlX, y, span, 4000.f);
      Gdiplus::RectF bounds;
      graphics.MeasureString(text, -1, &small, layout, &logFormat, &bounds);
      return std::max(dp(22), bounds.Height + dp(4));
    };
    const float copyrightHeight = blockHeight(copyright);
    drawText(graphics, copyright, small, RectF(controlX, y, span, copyrightHeight), colorOf(90, 90, 90), StringAlignmentNear, true);
    y += copyrightHeight + dp(12);
    drawText(graphics, S(L"使用协议", L"Terms"), body, RectF(controlX, y, span, dp(22)), colorOf(70, 70, 70), StringAlignmentNear);
    y += dp(24);
    const float termsHeight = blockHeight(terms);
    drawText(graphics, terms, small, RectF(controlX, y, span, termsHeight), colorOf(90, 90, 90), StringAlignmentNear, true);
    y += termsHeight + dp(12);
    drawText(graphics, S(L"更新日志", L"Changes"), body, RectF(controlX, y, span, dp(22)), colorOf(70, 70, 70), StringAlignmentNear);
    y += dp(24);
    const std::wstring log = changelogText();
    const float logHeight = blockHeight(log.c_str());
    drawText(graphics, log.c_str(), small, RectF(controlX, y, span, logHeight), colorOf(90, 90, 90), StringAlignmentNear, true);
    y += logHeight + dp(8);
  }
  const float footerH = dp(48);
  const float footerTop = static_cast<float>(height) - footerH;
  contentBottom = static_cast<int>(y + scroll + dp(8) + footerH);
  SolidBrush footer(colorOf(236, 236, 236));
  graphics.FillRectangle(&footer, side, footerTop, static_cast<float>(width) - side, footerH);
  Pen footerLine(colorOf(214, 214, 214), 1.f);
  graphics.DrawLine(&footerLine, side, footerTop, static_cast<float>(width), footerTop);
  RectF confirm(static_cast<float>(width) - dp(132), footerTop + dp(8), dp(108), dp(32));
  SolidBrush confirmFill(colorOf(214, 52, 52));
  graphics.FillRectangle(&confirmFill, confirm);
  drawText(graphics, S(L"确认", L"Confirm"), body, confirm, colorOf(255, 255, 255), StringAlignmentCenter);
  addHit(kConfirm, confirm);

  Pen xpen(colorOf(80, 80, 80), 1.6f);
  const float x0 = width - dp(32);
  const float y0 = dp(18);
  graphics.DrawLine(&xpen, x0, y0, x0 + dp(14), y0 + dp(14));
  graphics.DrawLine(&xpen, x0 + dp(14), y0, x0, y0 + dp(14));
  addHit(kClose, RectF(x0 - dp(8), y0 - dp(8), dp(30), dp(30)));
  if (openMenu >= kMenuSync) drawMenu(graphics, body);
}

bool contains(const Gdiplus::RectF& rect, int x, int y) {
  return x >= rect.X && y >= rect.Y && x < rect.X + rect.Width && y < rect.Y + rect.Height;
}

int hitTest(int x, int y) {
  for (int index = static_cast<int>(hits.size()) - 1; index >= 0; index -= 1) {
    if (contains(hits[index].rect, x, y)) return hits[index].id;
  }
  return 0;
}

Gdiplus::RectF findHit(int id) {
  for (const Hit& hit : hits) {
    if (hit.id == id) return hit.rect;
  }
  return {};
}

std::wstring editText(HWND edit) {
  wchar_t buffer[240];
  GetWindowTextW(edit, buffer, 240);
  return buffer;
}

void readEdits() {
  if (hoverEdit && IsWindowVisible(hoverEdit)) state.hoverSeconds = editText(hoverEdit);
  if (switch1Edit && IsWindowVisible(switch1Edit)) state.switchPrimary = editText(switch1Edit);
  if (switch2Edit && IsWindowVisible(switch2Edit)) state.switchSecondary = editText(switch2Edit);
  if (formatEdit && IsWindowVisible(formatEdit)) {
    if (openMenu == kMenuFormat1) state.formatPrimary = editText(formatEdit);
    if (openMenu == kMenuFormat2) state.formatSecondary = editText(formatEdit);
  }
  if (dateEdit && IsWindowVisible(dateEdit)) {
    int year = 0;
    int month = 0;
    int day = 0;
    if (std::swscanf(editText(dateEdit).c_str(), L"%d-%d-%d", &year, &month, &day) == 3 && isValidDate(year, month, day)) {
      state.customYear = year;
      state.customMonth = month;
      state.customDay = day;
    }
  }
}

void placeEdit(HWND edit, const EditPlace& place) {
  if (!edit) return;
  RECT client{};
  if (settingsWindow) GetClientRect(settingsWindow, &client);
  const int footerTop = client.bottom - static_cast<int>(dp(48));
  if (!place.show || (client.bottom > 0 && place.y + place.height > footerTop)) {
    if (IsWindowVisible(edit)) ShowWindow(edit, SW_HIDE);
    return;
  }
  RECT current{};
  GetWindowRect(edit, &current);
  POINT origin{current.left, current.top};
  ScreenToClient(GetParent(edit), &origin);
  const int width = current.right - current.left;
  const int height = current.bottom - current.top;
  if (!IsWindowVisible(edit) || origin.x != place.x || origin.y != place.y || width != place.width || height != place.height) {
    MoveWindow(edit, place.x, place.y, place.width, place.height, TRUE);
  }
  if (!IsWindowVisible(edit)) ShowWindow(edit, SW_SHOW);
}

void syncFormatEdit() {
  if (!formatEdit || formatPlace.show == false) return;
  if (formatSynced) return;
  SetWindowTextW(formatEdit, (openMenu == kMenuFormat2 ? state.formatSecondary : state.formatPrimary).c_str());
  SendMessageW(formatEdit, EM_SETSEL, 0, -1);
  formatSynced = true;
}

void applyMenuSelection(int index) {
  if (openMenu == kMenuHour) state.customHour = index;
  else if (openMenu == kMenuMinute) state.customMinute = index;
  else if (openMenu == kMenuSecond) state.customSecond = index;
  else if (openMenu == kMenuQuietFrom) state.quietFrom = index;
  else if (openMenu == kMenuQuietTo) state.quietTo = index;
  else if (openMenu == kMenuSync) state.syncMode = index;
  else if (openMenu == kMenuDpi) state.dpiMode = index;
  else if (openMenu == kMenuLanguage) state.language = index == 1 ? 1 : 0;
  else if (openMenu == kMenuInterval) {
    const int minutes[] = {1, 5, 10, 30, 60, 180, 480};
    if (index >= 0 && index < 7) state.backgroundInterval = minutes[index];
  }
}

void setOpacityFromPointer(int x) {
  if (opacityTrack.Width < 1.f) return;
  float portion = (static_cast<float>(x) - opacityTrack.X) / opacityTrack.Width;
  if (portion < 0.f) portion = 0.f;
  if (portion > 1.f) portion = 1.f;
  state.windowOpacity = 40 + static_cast<int>(portion * 60.f + 0.5f);
  applyWindowOpacity();
}

struct FontPick {
  std::wstring face;
  int points = 16;
  bool bold = false;
  bool italic = false;
};

std::wstring fontSummary(const std::wstring& face, int points, bool bold, bool italic) {
  std::wstring text = face.empty() ? L"Microsoft YaHei" : face;
  if (points < 8) return text;
  text += L"  ";
  if (bold && italic) text += S(L"粗斜体", L"Bold italic");
  else if (bold) text += S(L"粗体", L"Bold");
  else if (italic) text += S(L"斜体", L"Italic");
  else text += S(L"常规", L"Regular");
  text += L"  " + std::to_wstring(points);
  return text;
}

bool chooseFace(HWND window, FontPick& pick) {
  if (pick.points < 8 || pick.points > 72) pick.points = 16;
  HDC dc = GetDC(window);
  const int dpi = std::max(96, GetDeviceCaps(dc, LOGPIXELSY));
  ReleaseDC(window, dc);
  LOGFONTW font{};
  font.lfCharSet = DEFAULT_CHARSET;
  wcsncpy(font.lfFaceName, pick.face.c_str(), LF_FACESIZE - 1);
  font.lfHeight = -MulDiv(pick.points, dpi, 72);
  font.lfWeight = pick.bold ? FW_BOLD : FW_NORMAL;
  font.lfItalic = pick.italic ? TRUE : FALSE;
  CHOOSEFONTW choose{};
  choose.lStructSize = sizeof(choose);
  choose.hwndOwner = window;
  choose.lpLogFont = &font;
  choose.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOSCRIPTSEL;
  if (!ChooseFontW(&choose)) return false;
  pick.face = font.lfFaceName;
  pick.points = std::abs(MulDiv(font.lfHeight, 72, dpi));
  if (pick.points < 8) pick.points = 8;
  if (pick.points > 72) pick.points = 72;
  pick.bold = font.lfWeight >= FW_BOLD;
  pick.italic = font.lfItalic != FALSE;
  return true;
}

std::wstring changelogText() {
  static std::wstring text;
  static bool loaded = false;
  if (loaded) return text;
  loaded = true;
  std::ifstream input{std::filesystem::path(dataPath(L"changelog.txt")), std::ios::binary};
  std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF && static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) bytes.erase(0, 3);
  text = utf8ToWide(bytes);
  text.erase(std::remove(text.begin(), text.end(), L'\r'), text.end());
  if (text.empty()) text = S(L"还没有更新日志。", L"No changelog yet.");
  return text;
}

void readLocalTime() {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  state.customYear = now.wYear;
  state.customMonth = now.wMonth;
  state.customDay = now.wDay;
  state.customHour = now.wHour;
  state.customMinute = now.wMinute;
  state.customSecond = now.wSecond;
  wchar_t text[32];
  std::swprintf(text, 32, L"%04d-%02d-%02d", now.wYear, now.wMonth, now.wDay);
  if (dateEdit) SetWindowTextW(dateEdit, text);
  statusText = S(L"已读取本机时间", L"Read the computer clock");
}

void keepCustomTime() {
  readEdits();
  if (!isValidDate(state.customYear, state.customMonth, state.customDay)) {
    statusText = S(L"日期无效", L"Invalid date");
    return;
  }
  statusText = S(L"已保存自定义时间", L"Custom time saved");
}

HWND makeEdit(HWND parent, int id, bool center) {
  HWND edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL | (center ? ES_CENTER : ES_LEFT), 0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
  SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(editFont), TRUE);
  SendMessageW(edit, EM_SETLIMITTEXT, center ? 8 : 120, 0);
  return edit;
}

void openList(int menu, int hitId) {
  if (openMenu == menu) {
    openMenu = 0;
    return;
  }
  openMenu = menu;
  menuAnchor = findHit(hitId);
  menuScroll = std::max(0, menuSelected(menu) - 3);
  formatSynced = false;
}

LRESULT CALLBACK settingsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_CREATE: {
      settingsWindow = window;
      editBrush = CreateSolidBrush(RGB(255, 255, 255));
      editFont = CreateFontW(-static_cast<int>(dp(14)), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
      hoverEdit = makeEdit(window, kEditHover, true);
      switch1Edit = makeEdit(window, kEditSwitch1, true);
      switch2Edit = makeEdit(window, kEditSwitch2, true);
      formatEdit = makeEdit(window, kEditFormat, false);
      dateEdit = makeEdit(window, kEditDate, false);
      SetWindowTextW(hoverEdit, state.hoverSeconds.c_str());
      SetWindowTextW(switch1Edit, state.switchPrimary.c_str());
      SetWindowTextW(switch2Edit, state.switchSecondary.c_str());
      wchar_t date[32];
      std::swprintf(date, 32, L"%04d-%02d-%02d", state.customYear, state.customMonth, state.customDay);
      SetWindowTextW(dateEdit, date);
      return 0;
    }
    case WM_COMMAND:
      if (HIWORD(wParam) == EN_CHANGE) {
        readEdits();
        saveSettings();
        InvalidateRect(window, nullptr, FALSE);
      }
      return 0;
    case WM_CTLCOLOREDIT: {
      SetBkColor(reinterpret_cast<HDC>(wParam), RGB(255, 255, 255));
      SetTextColor(reinterpret_cast<HDC>(wParam), RGB(55, 55, 55));
      return reinterpret_cast<LRESULT>(editBrush);
    }
    case WM_MOUSEWHEEL: {
      const int delta = GET_WHEEL_DELTA_WPARAM(wParam) / 120;
      if (openMenu >= kMenuSync) menuScroll -= delta;
      else scroll = std::max(0, scroll - delta * static_cast<int>(dp(40)));
      InvalidateRect(window, nullptr, FALSE);
      return 0;
    }
    case WM_KEYDOWN:
      if (wParam == VK_ESCAPE) {
        if (openMenu != 0) openMenu = 0;
        else ShowWindow(window, SW_HIDE);
        invalidateCalendar();
        InvalidateRect(window, nullptr, FALSE);
      }
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT paint;
      HDC dc = BeginPaint(window, &paint);
      RECT client{};
      GetClientRect(window, &client);
      if (client.right > 0) settingsScale = client.right / kDesignWidth;
      const int limit = std::max(0, contentBottom - static_cast<int>(client.bottom));
      if (scroll > limit) scroll = limit;
      Gdiplus::Bitmap buffer(client.right, client.bottom, PixelFormat32bppPARGB);
      Graphics graphics(&buffer);
      paintSettings(graphics, client.right, client.bottom);
      Graphics screen(dc);
      screen.DrawImage(&buffer, 0, 0);
      EndPaint(window, &paint);
      placeEdit(hoverEdit, hoverPlace);
      placeEdit(switch1Edit, switch1Place);
      placeEdit(switch2Edit, switch2Place);
      placeEdit(formatEdit, formatPlace);
      placeEdit(dateEdit, datePlace);
      syncFormatEdit();
      const int designWidth = static_cast<int>(dp(kDesignWidth));
      const int designHeight = static_cast<int>(dp(kDesignHeight));
      if (std::abs(client.right - designWidth) > 2 || std::abs(client.bottom - designHeight) > 2) {
        RECT frame{};
        GetWindowRect(window, &frame);
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        int originX = frame.left;
        int originY = frame.top;
        if (originX + designWidth > work.right) originX = std::max(work.left, work.right - designWidth - 8);
        if (originY + designHeight > work.bottom) originY = std::max(work.top, work.bottom - designHeight - 8);
        SetWindowPos(window, nullptr, originX, originY, designWidth, designHeight, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        InvalidateRect(window, nullptr, FALSE);
      }
      return 0;
    }
    case WM_LBUTTONDOWN: {
      const int x = static_cast<short>(LOWORD(lParam));
      const int y = static_cast<short>(HIWORD(lParam));
      const int id = hitTest(x, y);
      if (id >= kMenuItem) {
        const int menu = openMenu;
        applyMenuSelection(id - kMenuItem);
        openMenu = 0;
        saveSettings();
        if (menu == kMenuLanguage) {
          invalidateCalendar();
          invalidateWeatherPanel();
          requestWeatherRefresh();
          refreshPlanWindow();
        } else if (menu == kMenuInterval) invalidateCalendar();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
      }
      if (id == kMenuBlock) return 0;
      if (id == kConfirm) {
        readEdits();
        if ((state.backgroundOrder || state.backgroundShuffle) && rotationClockChanged()) state.backgroundOrderStart = static_cast<long long>(std::time(nullptr));
        rememberRotationClock();
        saveSettings();
        openMenu = 0;
        applyWindowOpacity();
        updateDockClock();
        ShowWindow(window, SW_HIDE);
        invalidateCalendar();
        refreshPlanWindow();
        return 0;
      }
      if (id == kClose) {
        ShowWindow(window, SW_HIDE);
        invalidateCalendar();
        return 0;
      }
      if (id >= kPageBase && id < kPageBase + 6) {
        page = id - kPageBase;
        openMenu = 0;
        scroll = 0;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
      }
      if (y < dp(64) && x > dp(kSideWidth) && id == 0) {
        ReleaseCapture();
        SendMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
      }
      bool changed = true;
      bool redrawCalendar = false;
      switch (id) {
        case kLaunch:
          state.launchAtStartup = !state.launchAtStartup;
          setStartup(state.launchAtStartup);
          break;
        case kHour24:
          state.hour24 = !state.hour24;
          redrawCalendar = true;
          break;
        case kZodiacLichun: state.zodiacFromLichun = true; break;
        case kZodiacChuyi: state.zodiacFromLichun = false; break;
        case kWeekSunday:
          state.weekStartsMonday = false;
          redrawCalendar = true;
          break;
        case kWeekMonday:
          state.weekStartsMonday = true;
          redrawCalendar = true;
          break;
        case kFloat: state.showWeatherFloat = !state.showWeatherFloat; break;
        case kAlert: state.weatherAlert = !state.weatherAlert; break;
        case kFont: {
          FontPick pick{state.dockFont.empty() ? state.clockFont : state.dockFont, state.dockFontSize > 0 ? state.dockFontSize : 14, state.dockFontBold, state.dockFontItalic};
          if (chooseFace(window, pick)) {
            state.dockFont = pick.face;
            state.dockFontSize = pick.points;
            state.dockFontBold = pick.bold;
            state.dockFontItalic = pick.italic;
            saveSettings();
            updateDockClock();
          }
          break;
        }
        case kCalendarFont: {
          FontPick pick{state.clockFont, state.clockFontSize > 0 ? state.clockFontSize : 16, state.clockFontSize > 0 ? state.clockFontBold : true, state.clockFontItalic};
          if (chooseFace(window, pick)) {
            state.clockFont = pick.face;
            state.clockFontSize = pick.points;
            state.clockFontBold = pick.bold;
            state.clockFontItalic = pick.italic;
            saveSettings();
            invalidateCalendar();
          }
          break;
        }
        case kAlignLeft: state.clockAlign = 0; break;
        case kAlignCenter: state.clockAlign = 1; break;
        case kAlignRight: state.clockAlign = 2; break;
        case kDock:
          state.dockClock = !state.dockClock;
          updateDockClock();
          break;
        case kOpacity:
          draggingOpacity = true;
          SetCapture(window);
          setOpacityFromPointer(x);
          break;
        case kModeSingle: state.dualScreen = false; break;
        case kModeDual: state.dualScreen = true; break;
        case kFormatBox:
        case kFormatHelp:
          openMenu = openMenu == kMenuFormat1 ? 0 : kMenuFormat1;
          formatSynced = false;
          break;
        case kFormat2Box:
          openMenu = openMenu == kMenuFormat2 ? 0 : kMenuFormat2;
          formatSynced = false;
          break;
        case kSwitchUp: state.switchUpward = true; break;
        case kSwitchSide: state.switchUpward = false; break;
        case kTimeAnn: state.timeAnnounce = !state.timeAnnounce; break;
        case kHourly:
          if (!state.timeAnnounce) changed = false;
          else state.announceMode = 0;
          break;
        case kHalf:
          if (!state.timeAnnounce) changed = false;
          else state.announceMode = 1;
          break;
        case kTen:
          if (!state.timeAnnounce) changed = false;
          else state.announceMode = 2;
          break;
        case kMinuteAnn:
          if (!state.timeAnnounce) changed = false;
          else state.announceMode = 3;
          break;
        case kWeatherAnn: state.weatherAnnounce = !state.weatherAnnounce; break;
        case kQuiet: state.quietMode = !state.quietMode; break;
        case kFullSound: state.soundInFullscreen = !state.soundInFullscreen; break;
        case kSyncNow: readLocalTime(); break;
        case kSetTime: keepCustomTime(); break;
        case kPush: state.receivePush = !state.receivePush; break;
        case kThemeNight:
          state.appearance = 0;
          redrawCalendar = true;
          break;
        case kThemeDay:
          state.appearance = 1;
          redrawCalendar = true;
          break;
        case kThemeImage:
          state.appearance = 2;
          redrawCalendar = true;
          break;
        case kShuffle:
          state.backgroundShuffle = !state.backgroundShuffle;
          if (state.backgroundShuffle) state.backgroundOrder = false;
          redrawCalendar = true;
          break;
        case kOrder:
          state.backgroundOrder = !state.backgroundOrder;
          if (state.backgroundOrder) state.backgroundShuffle = false;
          redrawCalendar = true;
          break;
        case kCropLeft:
          state.backgroundCrop = 0;
          redrawCalendar = true;
          break;
        case kCropCenter:
          state.backgroundCrop = 1;
          redrawCalendar = true;
          break;
        case kCropRight:
          state.backgroundCrop = 2;
          redrawCalendar = true;
          break;
        case kPickImage: {
          wchar_t file[MAX_PATH]{};
          if (state.backgroundImage.size() < MAX_PATH) std::wcsncpy(file, state.backgroundImage.c_str(), MAX_PATH - 1);
          OPENFILENAMEW dialog{};
          dialog.lStructSize = sizeof(dialog);
          dialog.hwndOwner = window;
          dialog.lpstrFilter = state.language == 1
              ? L"Pictures (*.jpg;*.jpeg;*.png;*.bmp;*.gif);Video (*.mp4;*.m4v;*.wmv;*.avi;*.mkv;*.mov)\0*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.mp4;*.m4v;*.wmv;*.avi;*.mkv;*.mov\0Pictures (*.jpg;*.jpeg;*.png;*.bmp;*.gif)\0*.jpg;*.jpeg;*.png;*.bmp;*.gif\0Video (*.mp4;*.m4v;*.wmv;*.avi;*.mkv;*.mov)\0*.mp4;*.m4v;*.wmv;*.avi;*.mkv;*.mov\0\0"
              : L"图片 (*.jpg;*.jpeg;*.png;*.bmp;*.gif);视频 (*.mp4;*.m4v;*.wmv;*.avi;*.mkv;*.mov)\0*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.mp4;*.m4v;*.wmv;*.avi;*.mkv;*.mov\0图片 (*.jpg;*.jpeg;*.png;*.bmp;*.gif)\0*.jpg;*.jpeg;*.png;*.bmp;*.gif\0视频 (*.mp4;*.m4v;*.wmv;*.avi;*.mkv;*.mov)\0*.mp4;*.m4v;*.wmv;*.avi;*.mkv;*.mov\0\0";
          dialog.lpstrFile = file;
          dialog.nMaxFile = MAX_PATH;
          dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
          if (GetOpenFileNameW(&dialog)) {
            if (backgroundVideoTooLarge(file)) {
              MessageBoxW(window, state.language == 1 ? L"Video files must be 20 MB or smaller." : L"视频不能超过 20 MB，请另选一个较短的文件。", state.language == 1 ? L"Background" : L"背景", MB_OK | MB_ICONINFORMATION);
              changed = false;
            } else {
              state.backgroundImage = file;
              state.appearance = 2;
              redrawCalendar = true;
            }
          } else changed = false;
          break;
        }
        case kQWeather:
          promptQWeatherCredentials(window, true);
          changed = false;
          break;
        case kMenuSync:
        case kMenuHour:
        case kMenuMinute:
        case kMenuSecond:
        case kMenuQuietFrom:
        case kMenuQuietTo:
        case kMenuDpi:
        case kMenuLanguage:
        case kMenuInterval:
          openList(id, id);
          break;
        default: changed = false; break;
      }
      if (changed) {
        saveSettings();
        if (redrawCalendar) {
          invalidateCalendar();
          invalidateWeatherPanel();
        }
        InvalidateRect(window, nullptr, FALSE);
      }
      return 0;
    }
    case WM_MOUSEMOVE:
      if (draggingOpacity) {
        setOpacityFromPointer(static_cast<short>(LOWORD(lParam)));
        InvalidateRect(window, nullptr, FALSE);
      }
      return 0;
    case WM_LBUTTONUP:
      if (draggingOpacity) {
        draggingOpacity = false;
        ReleaseCapture();
        saveSettings();
      }
      return 0;
    case WM_CLOSE:
      ShowWindow(window, SW_HIDE);
      invalidateCalendar();
      return 0;
    case WM_DESTROY:
      if (editFont) DeleteObject(editFont);
      if (editBrush) DeleteObject(editBrush);
      editFont = nullptr;
      editBrush = nullptr;
      settingsWindow = nullptr;
      return 0;
    default:
      return DefWindowProcW(window, message, wParam, lParam);
  }
}

}

LOGFONTW styledLogFont(const std::wstring& face, float pixels, bool bold, bool italic) {
  LOGFONTW font{};
  font.lfCharSet = DEFAULT_CHARSET;
  font.lfQuality = CLEARTYPE_QUALITY;
  font.lfOutPrecision = OUT_TT_PRECIS;
  font.lfHeight = -std::max(1, static_cast<int>(pixels + 0.5f));
  font.lfWeight = bold ? FW_BOLD : FW_NORMAL;
  font.lfItalic = italic ? TRUE : FALSE;
  const std::wstring name = face.empty() ? L"Microsoft YaHei" : face;
  wcsncpy(font.lfFaceName, name.c_str(), LF_FACESIZE - 1);
  return font;
}

CalendarSettings& calendarSettings() {
  prepare();
  return state;
}

void persistCalendarSettings() {
  prepare();
  state.announceHourly = state.announceMode == 0;
  saveSettings();
}

std::wstring activeClockText() {
  prepare();
  static DWORD switchedAt = 0;
  static bool secondScreen = false;
  if (!state.dualScreen) secondScreen = false;
  else {
    int seconds = _wtoi(state.switchPrimary.c_str());
    if (seconds < 1) seconds = 5;
    const DWORD now = GetTickCount();
    if (switchedAt == 0 || now - switchedAt >= static_cast<DWORD>(seconds) * 1000) {
      if (switchedAt != 0) secondScreen = !secondScreen;
      switchedAt = now;
    }
  }
  return formatPreview(secondScreen ? state.formatSecondary : state.formatPrimary);
}

FittedWindow fitDesign(float designWidth, float designHeight) {
  HDC screen = GetDC(nullptr);
  const int dpi = GetDeviceCaps(screen, LOGPIXELSX);
  ReleaseDC(nullptr, screen);
  const int screenW = GetSystemMetrics(SM_CXSCREEN);
  const int screenH = GetSystemMetrics(SM_CYSCREEN);
  float fit = 1.f;
  if (screenW > 0 && screenH > 0) {
    const float sx = static_cast<float>(screenW) / static_cast<float>(kBaseScreenWidth);
    const float sy = static_cast<float>(screenH) / static_cast<float>(kBaseScreenHeight);
    fit = std::min(sx, sy);
  }
  const float dpiScale = dpi > 0 ? dpi / 96.f : 1.f;
  float scale = dpiScale * fit;
  int width = std::max(1, static_cast<int>(designWidth * scale + 0.5f));
  int height = std::max(1, static_cast<int>(designHeight * scale + 0.5f));
  RECT work{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  const int availW = std::max(1, static_cast<int>(work.right - work.left) - 8);
  const int availH = std::max(1, static_cast<int>(work.bottom - work.top) - 8);
  if (width > availW || height > availH) {
    const float shrink = std::min(static_cast<float>(availW) / static_cast<float>(width), static_cast<float>(availH) / static_cast<float>(height));
    scale *= shrink;
    width = std::max(1, static_cast<int>(designWidth * scale + 0.5f));
    height = std::max(1, static_cast<int>(designHeight * scale + 0.5f));
  }
  return {width, height, scale};
}

void moveFittedWindow(HWND window, const FittedWindow& fitted) {
  if (!window) return;
  RECT frame{};
  GetWindowRect(window, &frame);
  RECT work{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  int x = frame.left;
  int y = frame.top;
  if (x + fitted.width > work.right) x = work.right - fitted.width;
  if (y + fitted.height > work.bottom) y = work.bottom - fitted.height;
  if (x < work.left) x = work.left;
  if (y < work.top) y = work.top;
  if (frame.left == x && frame.top == y && frame.right - frame.left == fitted.width && frame.bottom - frame.top == fitted.height) return;
  SetWindowPos(window, nullptr, x, y, fitted.width, fitted.height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
  InvalidateRect(window, nullptr, FALSE);
}

void applySettingsWindowSize() {
  if (!settingsWindow) return;
  const FittedWindow fitted = fitDesign(kDesignWidth, kDesignHeight);
  settingsScale = fitted.scale;
  moveFittedWindow(settingsWindow, fitted);
}

bool settingsWindowVisible() {
  return settingsWindow && IsWindowVisible(settingsWindow);
}

void showSettingsWindow(HWND owner) {
  prepare();
  const bool opening = !settingsWindow || !IsWindowVisible(settingsWindow);
  if (opening) rememberRotationClock();
  if (settingsWindow) {
    applySettingsWindowSize();
    ShowWindow(settingsWindow, SW_SHOW);
    SetWindowPos(settingsWindow, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    InvalidateRect(settingsWindow, nullptr, FALSE);
    invalidateCalendar();
    return;
  }
  WNDCLASSW windowClass{};
  windowClass.lpfnWndProc = settingsProc;
  windowClass.hInstance = GetModuleHandleW(nullptr);
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.lpszClassName = L"YxCalendarSettings";
  RegisterClassW(&windowClass);
  const FittedWindow fitted = fitDesign(kDesignWidth, kDesignHeight);
  settingsScale = fitted.scale;
  const int width = fitted.width;
  const int height = fitted.height;
  RECT work{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  RECT parent{};
  if (owner) GetWindowRect(owner, &parent);
  int x = parent.right + 12;
  int y = parent.top;
  if (x + width > work.right) x = std::max(work.left, (work.right - work.left - width) / 2);
  if (y + height > work.bottom) y = std::max(work.top, work.bottom - height - 8);
  settingsWindow = CreateWindowExW(
      WS_EX_TOOLWINDOW,
      windowClass.lpszClassName,
      L"设置",
      WS_POPUP | WS_CLIPCHILDREN,
      x,
      y,
      width,
      height,
      owner,
      nullptr,
      windowClass.hInstance,
      nullptr);
  if (!settingsWindow) return;
  ShowWindow(settingsWindow, SW_SHOW);
  invalidateCalendar();
}

namespace {
constexpr int kHostEdit = 2101;
constexpr int kKidEdit = 2102;
constexpr int kSaveQWeather = 2103;
constexpr int kUseOpenMeteo = 2104;
constexpr int kIssuerEdit = 2105;
constexpr int kProjectEdit = 2106;
constexpr int kPrivateEdit = 2107;
constexpr int kPublicEdit = 2108;
constexpr int kGenerateKey = 2109;
HWND qweatherDialog = nullptr;

bool qweatherReady() {
  return !state.qweatherHost.empty() && !state.qweatherKid.empty() && !state.qweatherIssuer.empty() && !state.qweatherProject.empty() && state.qweatherPrivate.find(L"PRIVATE KEY") != std::wstring::npos;
}

std::wstring plainText(HWND control) {
  const int length = GetWindowTextLengthW(control);
  if (length <= 0) return {};
  std::wstring text(length, L'\0');
  GetWindowTextW(control, text.data(), length + 1);
  while (!text.empty() && (text.front() == L' ' || text.front() == L'\r' || text.front() == L'\n' || text.front() == L'\t')) text.erase(text.begin());
  while (!text.empty() && (text.back() == L' ' || text.back() == L'\r' || text.back() == L'\n' || text.back() == L'\t')) text.pop_back();
  return text;
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

std::wstring readTextFile(const std::wstring& path) {
  std::ifstream input{std::filesystem::path(path), std::ios::binary};
  if (!input) return {};
  const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  return utf8ToWide(bytes);
}

std::wstring controlText(HWND control) {
  const int length = GetWindowTextLengthW(control);
  if (length <= 0) return {};
  std::wstring text(length, L'\0');
  GetWindowTextW(control, text.data(), length + 1);
  while (!text.empty() && (text.front() == L' ' || text.front() == L'\t')) text.erase(text.begin());
  while (!text.empty() && (text.back() == L' ' || text.back() == L'\t' || text.back() == L'/')) text.pop_back();
  if (text.rfind(L"https://", 0) == 0) text = text.substr(8);
  if (text.rfind(L"http://", 0) == 0) text = text.substr(7);
  return text;
}

LRESULT CALLBACK qweatherProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_COMMAND:
      if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == kGenerateKey) {
        const std::wstring openssl = opensslExecutable();
        const std::wstring priv = dataPath(L"qweather_private.pem");
        const std::wstring pub = dataPath(L"qweather_public.pem");
        const std::wstring quote = L"\"";
        if (!runHidden(quote + openssl + quote + L" genpkey -algorithm ED25519 -out " + quote + priv + quote)) return 0;
        if (!runHidden(quote + openssl + quote + L" pkey -pubout -in " + quote + priv + quote + L" -out " + quote + pub + quote)) return 0;
        SetWindowTextW(GetDlgItem(window, kPrivateEdit), readTextFile(priv).c_str());
        SetWindowTextW(GetDlgItem(window, kPublicEdit), readTextFile(pub).c_str());
        return 0;
      }
      if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == kSaveQWeather) {
        state.qweatherHost = controlText(GetDlgItem(window, kHostEdit));
        state.qweatherKid = controlText(GetDlgItem(window, kKidEdit));
        state.qweatherIssuer = controlText(GetDlgItem(window, kIssuerEdit));
        state.qweatherProject = controlText(GetDlgItem(window, kProjectEdit));
        state.qweatherPrivate = plainText(GetDlgItem(window, kPrivateEdit));
        state.qweatherSkip = false;
        saveSettings();
        DestroyWindow(window);
        return 0;
      }
      if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == kUseOpenMeteo) {
        state.qweatherSkip = true;
        saveSettings();
        DestroyWindow(window);
        return 0;
      }
      break;
    case WM_CLOSE:
      state.qweatherSkip = true;
      saveSettings();
      DestroyWindow(window);
      return 0;
    case WM_DESTROY:
      if (qweatherDialog == window) qweatherDialog = nullptr;
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window, message, wParam, lParam);
}
}

void promptQWeatherCredentials(HWND owner, bool force) {
  prepare();
  if (!force && (state.qweatherSkip || qweatherReady())) return;

  WNDCLASSW windowClass{};
  windowClass.lpfnWndProc = qweatherProc;
  windowClass.hInstance = GetModuleHandleW(nullptr);
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  windowClass.lpszClassName = L"YxCalendarQWeather";
  RegisterClassW(&windowClass);

  RECT ownerRect{};
  GetWindowRect(owner, &ownerRect);
  const int width = 560;
  const int height = 640;
  const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
  const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
  qweatherDialog = CreateWindowExW(
      WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
      windowClass.lpszClassName,
      englishUi() ? L"QWeather" : L"和风天气",
      WS_POPUP | WS_CAPTION | WS_SYSMENU,
      x,
      y,
      width,
      height,
      owner,
      nullptr,
      windowClass.hInstance,
      nullptr);
  if (!qweatherDialog) return;

  HFONT font = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"微软雅黑");
  auto add = [&](const wchar_t* klass, const wchar_t* text, DWORD style, int id, int left, int top, int w, int h) {
    HWND child = CreateWindowExW(0, klass, text, WS_CHILD | WS_VISIBLE | style, left, top, w, h, qweatherDialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), windowClass.hInstance, nullptr);
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return child;
  };
  add(L"STATIC", S(L"API Host（控制台 - 设置）", L"API host"), 0, 0, 16, 8, 520, 20);
  add(L"EDIT", state.qweatherHost.c_str(), WS_BORDER | ES_AUTOHSCROLL, kHostEdit, 16, 28, 512, 26);
  add(L"STATIC", S(L"凭据 ID kid（控制台 - 项目管理 - 凭据）", L"Credential ID (kid)"), 0, 0, 16, 58, 520, 20);
  add(L"EDIT", state.qweatherKid.c_str(), WS_BORDER | ES_AUTOHSCROLL, kKidEdit, 16, 78, 512, 26);
  add(L"STATIC", S(L"开发者 ID iss（控制台 - 设置，Q 开头）", L"Developer ID (iss)"), 0, 0, 16, 108, 520, 20);
  add(L"EDIT", state.qweatherIssuer.c_str(), WS_BORDER | ES_AUTOHSCROLL, kIssuerEdit, 16, 128, 512, 26);
  add(L"STATIC", S(L"项目 ID sub（控制台 - 项目管理）", L"Project ID (sub)"), 0, 0, 16, 158, 520, 20);
  add(L"EDIT", state.qweatherProject.c_str(), WS_BORDER | ES_AUTOHSCROLL, kProjectEdit, 16, 178, 512, 26);
  add(L"STATIC", S(L"Ed25519 私钥", L"Ed25519 private key"), 0, 0, 16, 210, 250, 20);
  add(L"EDIT", state.qweatherPrivate.c_str(), WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL, kPrivateEdit, 16, 230, 512, 78);
  add(L"BUTTON", S(L"生成密钥对", L"New key pair"), 0, kGenerateKey, 16, 316, 140, 30);
  add(L"STATIC", S(L"公钥：粘贴到控制台「添加凭据 - JSON Web Token」", L"Public key: paste into the console credential"), 0, 0, 164, 320, 360, 22);
  add(L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, kPublicEdit, 16, 352, 512, 72);
  add(L"STATIC", S(L"使用 JWT（Authorization: Bearer）。每月请求控制在 40000 次以内。", L"JWT bearer auth. Stay under 40000 requests a month."), 0, 0, 16, 432, 512, 36);
  add(L"BUTTON", S(L"保存并使用和风", L"Save QWeather"), BS_DEFPUSHBUTTON, kSaveQWeather, 16, 478, 200, 34);
  add(L"BUTTON", S(L"使用 Open-Meteo", L"Use Open-Meteo"), 0, kUseOpenMeteo, 232, 478, 180, 34);

  EnableWindow(owner, FALSE);
  ShowWindow(qweatherDialog, SW_SHOW);
  UpdateWindow(qweatherDialog);
  MSG message;
  while (qweatherDialog && IsWindow(qweatherDialog)) {
    const BOOL got = GetMessageW(&message, nullptr, 0, 0);
    if (got == 0) {
      PostQuitMessage(static_cast<int>(message.wParam));
      break;
    }
    if (got == -1) break;
    if (!IsDialogMessageW(qweatherDialog, &message)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
  if (font) DeleteObject(font);
  EnableWindow(owner, TRUE);
  SetForegroundWindow(owner);
}
