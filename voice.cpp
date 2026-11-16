#include "voice.hpp"

#include "extra.hpp"

#include "log.hpp"
#include "paths.hpp"
#include "settings.hpp"

#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <gdiplus.h>

#include <algorithm>
#include <cwchar>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr int kPlanClose = 1;
constexpr int kPlanPage = 10;
constexpr int kPlanEnable = 20;
constexpr int kPlanHour = 21;
constexpr int kPlanMinute = 22;
constexpr int kPlanVoice = 23;
constexpr int kPlanBrowse = 24;
constexpr int kPlanPreview = 25;
constexpr int kPlanTimerH = 26;
constexpr int kPlanTimerM = 27;
constexpr int kPlanTimerS = 28;
constexpr int kPlanStart = 29;
constexpr int kPlanReset = 30;
constexpr int kPlanPreset = 31;
constexpr int kPlanTimerVoice = 36;
constexpr int kPlanTimerBrowse = 37;
constexpr int kPlanTimerPreview = 38;
constexpr int kPlanPause = 39;
constexpr int kPlanConfirm = 40;
constexpr int kEditTimerH = 81;
constexpr int kEditTimerM = 82;
constexpr int kEditTimerS = 83;
constexpr int kPlanMenuBlock = 1900;
constexpr int kPlanMenuItem = 2000;

enum class TimerPhase { Idle, Running, Paused, Done };

struct EditPlace {
  bool show = false;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

HWND planWindow = nullptr;
HWND hourEdit = nullptr;
HWND minuteEdit = nullptr;
HWND secondEdit = nullptr;
HFONT planEditFont = nullptr;
HBRUSH planEditBrush = nullptr;
bool syncingEdits = false;
EditPlace hourPlace;
EditPlace minutePlace;
EditPlace secondPlace;
float planScale = 1.f;
int planPage = 0;
int openMenu = 0;
int menuScroll = 0;
Gdiplus::RectF menuAnchor;
bool presetLoaded = false;
int timerHours = 0;
int timerMinutes = 5;
int timerSeconds = 0;
int remainMs = 5 * 60 * 1000;
ULONGLONG timerEndTick = 0;
bool timerRung = false;
TimerPhase timerPhase = TimerPhase::Idle;

struct Hit {
  int id = 0;
  Gdiplus::RectF rect;
};

std::vector<Hit> hits;

float dp(float value) {
  return value * planScale;
}

std::wstring soundsRoot() {
  return dataPath(L"Sounds");
}

bool fileExists(const std::wstring& path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring voiceName(const std::wstring& voice) {
  const std::wstring::size_type slash = voice.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return voice;
  return voice.substr(slash + 1);
}

std::wstring parentDirectory(const std::wstring& path) {
  const std::wstring::size_type slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return L"";
  return path.substr(0, slash);
}

std::wstring firstExisting(const std::wstring& first, const std::wstring& second) {
  if (fileExists(first)) return first;
  if (fileExists(second)) return second;
  return L"";
}

std::wstring clockName(int hour, int minute) {
  wchar_t name[16];
  std::swprintf(name, 16, L"%02d%02d.mp3", hour, minute);
  return name;
}

std::wstring announcementPath(int mode, int hour, int minute) {
  const std::wstring root = soundsRoot();
  const std::wstring name = clockName(hour, minute);
  if (mode == 3) return firstExisting(root + L"\\sounds\\" + name, root + L"\\" + name);
  return firstExisting(root + L"\\Serif\\" + name, root + L"\\serif\\" + name);
}

std::wstring alarmDirectory() {
  const std::wstring root = soundsRoot();
  if (GetFileAttributesW((root + L"\\Alarm").c_str()) != INVALID_FILE_ATTRIBUTES) return root + L"\\Alarm";
  return root + L"\\alarm";
}

std::vector<std::wstring> alarmFiles() {
  std::vector<std::wstring> names;
  WIN32_FIND_DATAW data{};
  const HANDLE handle = FindFirstFileW((alarmDirectory() + L"\\*.mp3").c_str(), &data);
  if (handle == INVALID_HANDLE_VALUE) return names;
  do {
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) names.emplace_back(data.cFileName);
  } while (FindNextFileW(handle, &data));
  FindClose(handle);
  std::sort(names.begin(), names.end());
  return names;
}

std::wstring alarmPath(const std::wstring& voice) {
  if (voice.empty()) return L"";
  if (voice.find(L'\\') != std::wstring::npos || voice.find(L'/') != std::wstring::npos) return fileExists(voice) ? voice : L"";
  const std::wstring path = alarmDirectory() + L"\\" + voice;
  return fileExists(path) ? path : L"";
}

std::wstring randomAlarmPath() {
  const std::vector<std::wstring> names = alarmFiles();
  if (names.empty()) return L"";
  std::mt19937 generator{std::random_device{}()};
  std::uniform_int_distribution<std::size_t> pick(0, names.size() - 1);
  return alarmDirectory() + L"\\" + names[pick(generator)];
}

bool openVoiceFile(const std::wstring& path, const wchar_t* type) {
  wchar_t command[4096];
  if (type && type[0]) std::swprintf(command, 4096, L"open \"%ls\" type %ls alias calendarvoice", path.c_str(), type);
  else std::swprintf(command, 4096, L"open \"%ls\" alias calendarvoice", path.c_str());
  return mciSendStringW(command, nullptr, 0, nullptr) == 0;
}

void playVoiceFile(const std::wstring& path) {
  if (path.empty() || !fileExists(path)) {
    if (!path.empty()) logMissingResource(path);
    return;
  }
  mciSendStringW(L"close calendarvoice", nullptr, 0, nullptr);
  const wchar_t* types[] = {L"", L"mpegvideo", L"waveaudio"};
  bool opened = false;
  for (const wchar_t* type : types) {
    if (openVoiceFile(path, type)) {
      opened = true;
      break;
    }
    mciSendStringW(L"close calendarvoice", nullptr, 0, nullptr);
  }
  if (!opened || mciSendStringW(L"play calendarvoice from 0", nullptr, 0, nullptr) != 0) logAccessError(path, L"无法播放");
}

bool quietNow(const CalendarSettings& settings, const SYSTEMTIME& now) {
  if (!settings.quietMode || settings.quietFrom == settings.quietTo) return false;
  const int hour = now.wHour;
  if (settings.quietFrom < settings.quietTo) return hour >= settings.quietFrom && hour < settings.quietTo;
  return hour >= settings.quietFrom || hour < settings.quietTo;
}

bool blockedByFullscreen(const CalendarSettings& settings) {
  if (settings.soundInFullscreen) return false;
  const HWND foreground = GetForegroundWindow();
  if (!foreground) return false;
  DWORD processId = 0;
  GetWindowThreadProcessId(foreground, &processId);
  if (processId == GetCurrentProcessId()) return false;
  RECT windowRect{};
  if (!GetWindowRect(foreground, &windowRect)) return false;
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST), &info)) return false;
  const RECT& screen = info.rcMonitor;
  return windowRect.left <= screen.left + 2 && windowRect.top <= screen.top + 2 && windowRect.right >= screen.right - 2 && windowRect.bottom >= screen.bottom - 2;
}

bool announcementDue(int mode, int minute) {
  if (mode == 3) return true;
  if (mode == 2) return minute % 10 == 0;
  if (mode == 1) return minute == 30;
  return minute == 0;
}

void stopVoice() {
  mciSendStringW(L"close calendarvoice", nullptr, 0, nullptr);
}

const wchar_t* planText(const wchar_t* zh, const wchar_t* en) {
  return englishUi() ? en : zh;
}

using Gdiplus::Color;
using Gdiplus::Font;
using Gdiplus::FontStyleBold;
using Gdiplus::FontStyleRegular;
using Gdiplus::Graphics;
using Gdiplus::Pen;
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
  hits.push_back(Hit{id, rect});
}

bool contains(const RectF& rect, int x, int y) {
  return x >= rect.X && y >= rect.Y && x < rect.X + rect.Width && y < rect.Y + rect.Height;
}

int hitTest(int x, int y) {
  for (int index = static_cast<int>(hits.size()) - 1; index >= 0; index -= 1) {
    if (contains(hits[index].rect, x, y)) return hits[index].id;
  }
  return 0;
}

RectF findHit(int id) {
  for (const Hit& hit : hits) {
    if (hit.id == id) return hit.rect;
  }
  return RectF();
}

int durationMs() {
  return ((timerHours * 60 + timerMinutes) * 60 + timerSeconds) * 1000;
}

void loadPreset() {
  if (presetLoaded) return;
  const CalendarSettings& settings = calendarSettings();
  timerHours = std::max(0, std::min(23, settings.timerHours));
  timerMinutes = std::max(0, std::min(59, settings.timerMinutes));
  timerSeconds = std::max(0, std::min(59, settings.timerSeconds));
  remainMs = durationMs();
  presetLoaded = true;
}

void storeTimerPreset() {
  CalendarSettings& settings = calendarSettings();
  settings.timerHours = timerHours;
  settings.timerMinutes = timerMinutes;
  settings.timerSeconds = timerSeconds;
  persistCalendarSettings();
}

void ringTimer() {
  if (timerRung) return;
  timerRung = true;
  const std::wstring chosen = alarmPath(calendarSettings().timerVoice);
  const std::wstring alarm = chosen.empty() ? randomAlarmPath() : chosen;
  if (alarm.empty()) {
    logMissingResource(alarmDirectory());
    MessageBeep(MB_ICONEXCLAMATION);
  } else playVoiceFile(alarm);
  showDuePopup(planText(L"计时器", L"Timer"), planText(L"时间到", L"Time's up"), L"timeisup.gif");
}

void tickPlanTimer() {
  if (timerPhase != TimerPhase::Running) return;
  if (GetTickCount64() < timerEndTick) return;
  remainMs = 0;
  timerPhase = TimerPhase::Done;
  ringTimer();
  if (planWindow) InvalidateRect(planWindow, nullptr, FALSE);
}

LRESULT CALLBACK timerPumpProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_TIMER) {
    tickPlanTimer();
    if (timerPhase == TimerPhase::Running && planWindow && IsWindow(planWindow)) InvalidateRect(planWindow, nullptr, FALSE);
    return 0;
  }
  return DefWindowProcW(window, message, wParam, lParam);
}

void ensureTimerPump() {
  static HWND pump = nullptr;
  if (pump && IsWindow(pump)) return;
  WNDCLASSW windowClass{};
  windowClass.lpfnWndProc = timerPumpProc;
  windowClass.hInstance = GetModuleHandleW(nullptr);
  windowClass.lpszClassName = L"YxCalendarTimerPump";
  RegisterClassW(&windowClass);
  pump = CreateWindowExW(0, windowClass.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, windowClass.hInstance, nullptr);
  if (pump) SetTimer(pump, 1, 200, nullptr);
}

void startTimer() {
  loadPreset();
  ensureTimerPump();
  if (timerPhase == TimerPhase::Running) return;
  if (timerPhase != TimerPhase::Paused) remainMs = durationMs();
  if (remainMs <= 0) return;
  timerRung = false;
  timerEndTick = GetTickCount64() + static_cast<ULONGLONG>(remainMs);
  timerPhase = TimerPhase::Running;
}

void pauseTimer() {
  if (timerPhase != TimerPhase::Running) return;
  const ULONGLONG now = GetTickCount64();
  if (now >= timerEndTick) {
    remainMs = 0;
    timerPhase = TimerPhase::Done;
    ringTimer();
    return;
  }
  remainMs = static_cast<int>(timerEndTick - now);
  timerPhase = TimerPhase::Paused;
}

void resetTimer() {
  stopVoice();
  timerPhase = TimerPhase::Idle;
  timerRung = false;
  timerEndTick = 0;
  remainMs = durationMs();
}

void setDuration(int hours, int minutes, int seconds) {
  if (timerPhase == TimerPhase::Running) return;
  timerHours = std::max(0, std::min(23, hours));
  timerMinutes = std::max(0, std::min(59, minutes));
  timerSeconds = std::max(0, std::min(59, seconds));
  timerPhase = TimerPhase::Idle;
  timerRung = false;
  remainMs = durationMs();
  storeTimerPreset();
}

int shownMs() {
  if (timerPhase == TimerPhase::Running) {
    const ULONGLONG now = GetTickCount64();
    if (now >= timerEndTick) return 0;
    return static_cast<int>(timerEndTick - now);
  }
  if (timerPhase == TimerPhase::Paused) return remainMs;
  if (timerPhase == TimerPhase::Done) return 0;
  return durationMs();
}

std::wstring clockText(int milliseconds) {
  const int seconds = milliseconds <= 0 ? 0 : (milliseconds + 999) / 1000;
  wchar_t text[16];
  std::swprintf(text, 16, L"%02d:%02d:%02d", seconds / 3600, (seconds / 60) % 60, seconds % 60);
  return text;
}

std::wstring numberText(int value, const wchar_t* zhUnit, const wchar_t* enUnit) {
  wchar_t text[16];
  if (englishUi()) {
    if (enUnit && enUnit[0]) std::swprintf(text, 16, L"%02d%ls", value, enUnit);
    else std::swprintf(text, 16, L"%02d", value);
  } else std::swprintf(text, 16, L"%d%ls", value, zhUnit);
  return text;
}

void drawChevron(Graphics& graphics, float x, float y, Color color) {
  Pen pen(color, 1.4f);
  graphics.DrawLine(&pen, x, y, x + dp(5), y + dp(4));
  graphics.DrawLine(&pen, x + dp(5), y + dp(4), x + dp(10), y);
}

void drawCheck(Graphics& graphics, const RectF& rect, bool on) {
  Pen pen(colorOf(120, 120, 120), 1.3f);
  graphics.DrawRectangle(&pen, rect);
  if (!on) return;
  Pen mark(colorOf(60, 60, 60), 1.8f);
  graphics.DrawLine(&mark, rect.X + dp(3), rect.Y + rect.Height * 0.55f, rect.X + rect.Width * 0.42f, rect.Y + rect.Height - dp(3));
  graphics.DrawLine(&mark, rect.X + rect.Width * 0.42f, rect.Y + rect.Height - dp(3), rect.X + rect.Width - dp(3), rect.Y + dp(3));
}

void drawField(Graphics& graphics, const RectF& rect, const std::wstring& text, bool enabled) {
  SolidBrush fill(enabled ? colorOf(255, 255, 255) : colorOf(244, 244, 244));
  graphics.FillRectangle(&fill, rect);
  Pen border(colorOf(214, 214, 214), 1.f);
  graphics.DrawRectangle(&border, rect);
  Font font(L"Microsoft YaHei", dp(13), FontStyleRegular, UnitPixel);
  drawText(graphics, text.c_str(), font, RectF(rect.X + dp(8), rect.Y, rect.Width - dp(28), rect.Height), enabled ? colorOf(60, 60, 60) : colorOf(176, 176, 176), StringAlignmentNear);
  drawChevron(graphics, rect.X + rect.Width - dp(18), rect.Y + rect.Height / 2.f - dp(2), enabled ? colorOf(120, 120, 120) : colorOf(190, 190, 190));
}

enum class ActionIcon { Play, Pause, Reset };

void drawActionIcon(Graphics& graphics, ActionIcon icon, float x, float y, float size, Color color) {
  SolidBrush brush(color);
  if (icon == ActionIcon::Play) {
    const Gdiplus::PointF points[] = {
      {x + size * 0.22f, y + size * 0.14f},
      {x + size * 0.22f, y + size * 0.86f},
      {x + size * 0.86f, y + size * 0.50f},
    };
    graphics.FillPolygon(&brush, points, 3);
    return;
  }
  if (icon == ActionIcon::Pause) {
    const float bar = size * 0.22f;
    const float top = y + size * 0.14f;
    const float height = size * 0.72f;
    graphics.FillRectangle(&brush, x + size * 0.16f, top, bar, height);
    graphics.FillRectangle(&brush, x + size * 0.62f, top, bar, height);
    return;
  }
  Pen pen(color, std::max(1.3f, size * 0.12f));
  const float pad = size * 0.16f;
  graphics.DrawArc(&pen, x + pad, y + pad, size - pad * 2.f, size - pad * 2.f, 40.f, 280.f);
  const Gdiplus::PointF head[] = {
    {x + size * 0.58f, y},
    {x + size * 0.96f, y + size * 0.02f},
    {x + size * 0.70f, y + size * 0.32f},
  };
  graphics.FillPolygon(&brush, head, 3);
}

void drawActionButton(Graphics& graphics, const Font& font, const RectF& rect, const wchar_t* text, ActionIcon icon, bool primary, bool enabled) {
  Color textColor = colorOf(255, 255, 255);
  if (!enabled) {
    SolidBrush fill(colorOf(214, 214, 214));
    graphics.FillRectangle(&fill, rect);
  } else if (primary) {
    SolidBrush fill(colorOf(214, 52, 52));
    graphics.FillRectangle(&fill, rect);
  } else {
    textColor = colorOf(60, 60, 60);
    SolidBrush fill(colorOf(255, 255, 255));
    graphics.FillRectangle(&fill, rect);
    Pen border(colorOf(214, 214, 214), 1.f);
    graphics.DrawRectangle(&border, rect);
  }
  const float iconSize = std::min(dp(14), rect.Height - dp(10));
  StringFormat format;
  format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
  RectF measured;
  graphics.MeasureString(text, -1, &font, Gdiplus::PointF(0, 0), &format, &measured);
  const float gap = dp(6);
  const float textWidth = std::min(measured.Width, rect.Width - iconSize - gap - dp(8));
  const float left = rect.X + (rect.Width - iconSize - gap - textWidth) / 2.f;
  drawActionIcon(graphics, icon, left, rect.Y + (rect.Height - iconSize) / 2.f, iconSize, textColor);
  drawText(graphics, text, font, RectF(left + iconSize + gap, rect.Y, textWidth + dp(2), rect.Height), textColor, StringAlignmentNear);
}

void drawButton(Graphics& graphics, const Font& font, const RectF& rect, const wchar_t* text, bool primary, bool enabled) {
  if (!enabled) {
    SolidBrush fill(colorOf(214, 214, 214));
    graphics.FillRectangle(&fill, rect);
    drawText(graphics, text, font, rect, colorOf(255, 255, 255), StringAlignmentCenter);
    return;
  }
  if (primary) {
    SolidBrush fill(colorOf(214, 52, 52));
    graphics.FillRectangle(&fill, rect);
    drawText(graphics, text, font, rect, colorOf(255, 255, 255), StringAlignmentCenter);
    return;
  }
  SolidBrush fill(colorOf(255, 255, 255));
  graphics.FillRectangle(&fill, rect);
  Pen border(colorOf(214, 214, 214), 1.f);
  graphics.DrawRectangle(&border, rect);
  drawText(graphics, text, font, rect, colorOf(60, 60, 60), StringAlignmentCenter);
}

void drawMark(Graphics& graphics) {
  const float x = dp(16);
  const float y = dp(18);
  const int pixels = std::max(16, static_cast<int>(dp(36)));
  static HICON programIcon = nullptr;
  if (!programIcon) programIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, pixels, pixels, LR_DEFAULTCOLOR));
  if (programIcon) {
    HDC hdc = graphics.GetHDC();
    DrawIconEx(hdc, static_cast<int>(x), static_cast<int>(y), programIcon, pixels, pixels, 0, nullptr, DI_NORMAL);
    graphics.ReleaseHDC(hdc);
  }
  Font font(L"Microsoft YaHei", dp(18), FontStyleBold, UnitPixel);
  drawText(graphics, planText(L"蕊历", L"Calendar"), font, RectF(x + static_cast<float>(pixels) + dp(8), y, dp(110), static_cast<float>(pixels)), colorOf(45, 45, 45), StringAlignmentNear);
}

void appendVoiceItems(std::vector<std::wstring>& items, const std::wstring& voice) {
  items.push_back(planText(L"随机", L"Random"));
  for (const std::wstring& name : alarmFiles()) items.push_back(name);
  if (voice.empty()) return;
  for (std::size_t index = 1; index < items.size(); index += 1) {
    if (items[index] == voice) return;
  }
  items.push_back(voice);
}

std::vector<std::wstring> menuItems() {
  std::vector<std::wstring> items;
  if (openMenu == kPlanHour || openMenu == kPlanTimerH) {
    for (int index = 0; index < 24; index += 1) items.push_back(numberText(index, L"时", openMenu == kPlanTimerH ? L"h" : L""));
  } else if (openMenu == kPlanMinute || openMenu == kPlanTimerM) {
    for (int index = 0; index < 60; index += 1) items.push_back(numberText(index, L"分", openMenu == kPlanTimerM ? L"m" : L""));
  } else if (openMenu == kPlanTimerS) {
    for (int index = 0; index < 60; index += 1) items.push_back(numberText(index, L"秒", L"s"));
  } else if (openMenu == kPlanVoice || openMenu == kPlanTimerVoice) {
    const CalendarSettings& settings = calendarSettings();
    appendVoiceItems(items, openMenu == kPlanTimerVoice ? settings.timerVoice : settings.alarmVoice);
  }
  return items;
}

int menuSelected() {
  const CalendarSettings& settings = calendarSettings();
  if (openMenu == kPlanHour) return settings.alarmHour;
  if (openMenu == kPlanMinute) return settings.alarmMinute;
  if (openMenu == kPlanTimerH) return timerHours;
  if (openMenu == kPlanTimerM) return timerMinutes;
  if (openMenu == kPlanTimerS) return timerSeconds;
  if (openMenu == kPlanVoice || openMenu == kPlanTimerVoice) {
    const std::wstring& voice = openMenu == kPlanTimerVoice ? settings.timerVoice : settings.alarmVoice;
    if (voice.empty()) return 0;
    const std::vector<std::wstring> items = menuItems();
    for (int index = 1; index < static_cast<int>(items.size()); index += 1) {
      if (items[static_cast<std::size_t>(index)] == voice) return index;
    }
  }
  return 0;
}

void openList(int menu) {
  if (openMenu == menu) {
    openMenu = 0;
    return;
  }
  openMenu = menu;
  menuAnchor = findHit(menu);
  const int count = static_cast<int>(menuItems().size());
  const int visible = std::min(8, std::max(count, 1));
  const int maxScroll = std::max(0, count - visible);
  menuScroll = std::max(0, std::min(menuSelected() - 3, maxScroll));
}

void applyMenu(int index) {
  CalendarSettings& settings = calendarSettings();
  if (openMenu == kPlanHour && index >= 0 && index < 24) settings.alarmHour = index;
  else if (openMenu == kPlanMinute && index >= 0 && index < 60) settings.alarmMinute = index;
  else if (openMenu == kPlanVoice || openMenu == kPlanTimerVoice) {
    const std::vector<std::wstring> items = menuItems();
    std::wstring& voice = openMenu == kPlanTimerVoice ? settings.timerVoice : settings.alarmVoice;
    if (index <= 0 || index >= static_cast<int>(items.size())) voice.clear();
    else voice = items[static_cast<std::size_t>(index)];
  } else if (openMenu == kPlanTimerH && index >= 0 && index < 24) {
    setDuration(index, timerMinutes, timerSeconds);
    return;
  } else if (openMenu == kPlanTimerM && index >= 0 && index < 60) {
    setDuration(timerHours, index, timerSeconds);
    return;
  } else if (openMenu == kPlanTimerS && index >= 0 && index < 60) {
    setDuration(timerHours, timerMinutes, index);
    return;
  } else return;
  persistCalendarSettings();
}

void drawMenu(Graphics& graphics, const Font& font, int height) {
  if (openMenu < kPlanHour) return;
  const std::vector<std::wstring> items = menuItems();
  if (items.empty()) return;
  const float row = dp(28);
  const int visible = std::min(8, static_cast<int>(items.size()));
  const int maxScroll = std::max(0, static_cast<int>(items.size()) - visible);
  menuScroll = std::max(0, std::min(menuScroll, maxScroll));
  const float listHeight = row * static_cast<float>(visible);
  float top = menuAnchor.Y + menuAnchor.Height;
  if (top + listHeight > static_cast<float>(height) - dp(8)) top = menuAnchor.Y - listHeight;
  if (top < dp(4)) top = dp(4);
  RectF list(menuAnchor.X, top, std::max(menuAnchor.Width, dp(120)), listHeight);
  SolidBrush fill(colorOf(255, 255, 255));
  graphics.FillRectangle(&fill, list);
  Pen border(colorOf(210, 210, 210), 1.f);
  graphics.DrawRectangle(&border, list);
  addHit(kPlanMenuBlock, list);
  const int selected = menuSelected();
  for (int index = 0; index < visible; index += 1) {
    const int item = menuScroll + index;
    RectF rowRect(list.X + 1.f, list.Y + index * row, list.Width - 2.f, row);
    if (item == selected) {
      SolidBrush mark(colorOf(232, 240, 250));
      graphics.FillRectangle(&mark, rowRect);
    }
    const std::wstring label = voiceName(items[static_cast<std::size_t>(item)]);
    drawText(graphics, label.c_str(), font, RectF(rowRect.X + dp(8), rowRect.Y, rowRect.Width - dp(12), rowRect.Height), colorOf(55, 55, 55), StringAlignmentNear);
    addHit(kPlanMenuItem + item, rowRect);
  }
}

void paintPlan(Graphics& graphics, int width, int height) {
  loadPreset();
  hits.clear();
  hourPlace = {};
  minutePlace = {};
  secondPlace = {};
  graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
  SolidBrush content(colorOf(236, 236, 236));
  SolidBrush sidebar(colorOf(188, 188, 188));
  graphics.FillRectangle(&content, 0, 0, width, height);
  graphics.FillRectangle(&sidebar, 0.f, 0.f, dp(188), static_cast<float>(height));
  drawMark(graphics);
  Font nav(L"Microsoft YaHei", dp(15), FontStyleRegular, UnitPixel);
  Font navBold(L"Microsoft YaHei", dp(15), FontStyleBold, UnitPixel);
  const wchar_t* pages[] = {planText(L"闹钟", L"Alarm"), planText(L"计时器", L"Timer")};
  for (int index = 0; index < 2; index += 1) {
    const float y = dp(108) + index * dp(58);
    if (planPage == index) {
      SolidBrush bar(colorOf(226, 68, 68));
      graphics.FillRectangle(&bar, 0, static_cast<int>(y), static_cast<int>(dp(4)), static_cast<int>(dp(42)));
    }
    drawText(graphics, pages[index], planPage == index ? navBold : nav, RectF(dp(28), y, dp(140), dp(42)), colorOf(55, 55, 55), StringAlignmentNear);
    addHit(kPlanPage + index, RectF(0, y, dp(188), dp(50)));
  }

  Font body(L"Microsoft YaHei", dp(14), FontStyleRegular, UnitPixel);
  Font title(L"Microsoft YaHei", dp(16), FontStyleBold, UnitPixel);
  Font small(L"Microsoft YaHei", dp(13), FontStyleRegular, UnitPixel);
  const float labelX = dp(200);
  const float controlX = dp(300);
  const CalendarSettings& settings = calendarSettings();
  const bool timing = timerPhase == TimerPhase::Running;
  if (planPage == 0) {
    drawText(graphics, planText(L"闹钟", L"Alarm"), title, RectF(dp(220), dp(28), dp(240), dp(32)), colorOf(40, 40, 40), StringAlignmentNear);
    RectF box(controlX, dp(92), dp(16), dp(16));
    drawCheck(graphics, box, settings.alarmEnabled);
    drawText(graphics, planText(L"启用闹钟", L"Enable alarm"), body, RectF(controlX + dp(24), dp(86), dp(180), dp(28)), colorOf(60, 60, 60), StringAlignmentNear);
    addHit(kPlanEnable, RectF(controlX, dp(84), dp(200), dp(32)));
    drawText(graphics, planText(L"时间:", L"Time:"), body, RectF(labelX, dp(142), dp(88), dp(32)), colorOf(70, 70, 70), StringAlignmentFar);
    RectF hourBox(controlX, dp(142), dp(108), dp(32));
    RectF minuteBox(controlX + dp(120), dp(142), dp(108), dp(32));
    drawField(graphics, hourBox, numberText(settings.alarmHour, L"时", L""), true);
    drawField(graphics, minuteBox, numberText(settings.alarmMinute, L"分", L""), true);
    addHit(kPlanHour, hourBox);
    addHit(kPlanMinute, minuteBox);
    drawText(graphics, planText(L"语音:", L"Voice:"), body, RectF(labelX, dp(198), dp(88), dp(32)), colorOf(70, 70, 70), StringAlignmentFar);
    const std::wstring voice = settings.alarmVoice.empty() ? std::wstring(planText(L"随机", L"Random")) : voiceName(settings.alarmVoice);
    RectF voiceBox(controlX, dp(198), dp(268), dp(32));
    drawField(graphics, voiceBox, voice, true);
    addHit(kPlanVoice, voiceBox);
    RectF browse(voiceBox.X + voiceBox.Width + dp(8), dp(198), dp(96), dp(32));
    RectF preview(controlX, dp(252), dp(96), dp(32));
    drawButton(graphics, body, browse, planText(L"选择文件", L"Choose file"), true, true);
    drawButton(graphics, body, preview, planText(L"试听", L"Preview"), false, true);
    addHit(kPlanBrowse, browse);
    addHit(kPlanPreview, preview);
    drawText(graphics, planText(L"到点播放。语音留在“随机”时，每次从闹钟录音里抽一条。", L"Plays at the set time. Random picks one recording each time."), small, RectF(controlX, dp(304), dp(360), dp(52)), colorOf(120, 120, 120), StringAlignmentNear, true);
  } else {
    drawText(graphics, planText(L"计时器", L"Timer"), title, RectF(dp(220), dp(28), dp(240), dp(32)), colorOf(40, 40, 40), StringAlignmentNear);
    const std::wstring clock = clockText(shownMs());
    const Color clockColor = timerPhase == TimerPhase::Done ? colorOf(214, 52, 52) : colorOf(40, 40, 40);
    Font clockFont(L"Microsoft YaHei", dp(46), FontStyleBold, UnitPixel);
    drawText(graphics, clock.c_str(), clockFont, RectF(dp(188), dp(64), static_cast<float>(width) - dp(188), dp(64)), clockColor, StringAlignmentCenter);
    const wchar_t* status = planText(L"未开始", L"Ready");
    Color statusColor = colorOf(120, 120, 120);
    if (timerPhase == TimerPhase::Running) {
      status = planText(L"计时中", L"Counting");
      statusColor = colorOf(214, 52, 52);
    } else if (timerPhase == TimerPhase::Paused) status = planText(L"已暂停", L"Paused");
    else if (timerPhase == TimerPhase::Done) {
      status = planText(L"时间到", L"Time's up");
      statusColor = colorOf(214, 52, 52);
    }
    drawText(graphics, status, body, RectF(dp(188), dp(128), static_cast<float>(width) - dp(188), dp(26)), statusColor, StringAlignmentCenter);
    drawText(graphics, planText(L"时长:", L"Duration:"), body, RectF(labelX, dp(164), dp(88), dp(32)), colorOf(70, 70, 70), StringAlignmentFar);
    RectF hourBox(controlX, dp(164), dp(108), dp(32));
    RectF minuteBox(controlX + dp(116), dp(164), dp(108), dp(32));
    RectF secondBox(controlX + dp(232), dp(164), dp(108), dp(32));
    auto durationField = [&](const RectF& box, int value, const wchar_t* zh, const wchar_t* en, int hit, EditPlace& place) {
      drawField(graphics, box, timing ? numberText(value, zh, en) : L"", !timing);
      if (timing) return;
      drawText(graphics, planText(zh, en), small, RectF(box.X + dp(58), box.Y, dp(24), box.Height), colorOf(90, 90, 90), StringAlignmentCenter);
      place = {true, static_cast<int>(box.X + dp(2)), static_cast<int>(box.Y + dp(3)), static_cast<int>(dp(54)), static_cast<int>(box.Height - dp(6))};
      addHit(hit, box);
    };
    durationField(hourBox, timerHours, L"时", L"h", kPlanTimerH, hourPlace);
    durationField(minuteBox, timerMinutes, L"分", L"m", kPlanTimerM, minutePlace);
    durationField(secondBox, timerSeconds, L"秒", L"s", kPlanTimerS, secondPlace);
    const int presetValues[] = {1, 5, 10, 25};
    const wchar_t* presetZh[] = {L"1分", L"5分", L"10分", L"25分"};
    const wchar_t* presetEn[] = {L"1m", L"5m", L"10m", L"25m"};
    for (int index = 0; index < 4; index += 1) {
      RectF chip(controlX + index * dp(84), dp(208), dp(74), dp(32));
      const bool selected = !timing && timerHours == 0 && timerMinutes == presetValues[index] && timerSeconds == 0;
      drawButton(graphics, small, chip, planText(presetZh[index], presetEn[index]), selected, !timing);
      if (!timing) addHit(kPlanPreset + index, chip);
    }
    drawText(graphics, planText(L"语音:", L"Voice:"), body, RectF(labelX, dp(256), dp(88), dp(32)), colorOf(70, 70, 70), StringAlignmentFar);
    const std::wstring voice = settings.timerVoice.empty() ? std::wstring(planText(L"随机", L"Random")) : voiceName(settings.timerVoice);
    RectF voiceBox(controlX, dp(256), dp(268), dp(32));
    drawField(graphics, voiceBox, voice, true);
    addHit(kPlanTimerVoice, voiceBox);
    RectF browse(voiceBox.X + voiceBox.Width + dp(8), dp(256), dp(96), dp(32));
    RectF preview(controlX, dp(300), dp(96), dp(32));
    drawButton(graphics, body, browse, planText(L"选择文件", L"Choose file"), true, true);
    drawButton(graphics, body, preview, planText(L"试听", L"Preview"), false, true);
    addHit(kPlanTimerBrowse, browse);
    addHit(kPlanTimerPreview, preview);
    const bool canStart = !timing && (timerPhase == TimerPhase::Paused ? remainMs > 0 : durationMs() > 0);
    const wchar_t* startLabel = timerPhase == TimerPhase::Paused ? planText(L"继续", L"Resume") : planText(L"开始", L"Start");
    const float actionY = dp(348);
    const float actionW = dp(108);
    RectF start(controlX, actionY, actionW, dp(32));
    RectF pause(controlX + dp(116), actionY, actionW, dp(32));
    RectF reset(controlX + dp(232), actionY, actionW, dp(32));
    drawActionButton(graphics, body, start, startLabel, ActionIcon::Play, true, canStart);
    drawActionButton(graphics, body, pause, planText(L"暂停", L"Pause"), ActionIcon::Pause, false, timing);
    drawActionButton(graphics, body, reset, planText(L"复位", L"Reset"), ActionIcon::Reset, false, true);
    if (canStart) addHit(kPlanStart, start);
    if (timing) addHit(kPlanPause, pause);
    addHit(kPlanReset, reset);
    drawText(graphics, planText(L"可直接输入数字，也可点右侧箭头选择。到点播放这条语音，与闹钟互不影响。关掉窗口也会继续计时。", L"Type a number or use the arrow. This voice is separate from the alarm. Closing the window keeps it running."), small, RectF(controlX, dp(392), dp(380), dp(56)), colorOf(120, 120, 120), StringAlignmentNear, true);
  }

  const float footerTop = static_cast<float>(height) - dp(56);
  SolidBrush footer(colorOf(236, 236, 236));
  graphics.FillRectangle(&footer, dp(188), footerTop, static_cast<float>(width) - dp(188), dp(56));
  Pen footerLine(colorOf(214, 214, 214), 1.f);
  graphics.DrawLine(&footerLine, dp(188), footerTop, static_cast<float>(width), footerTop);
  RectF confirm(static_cast<float>(width) - dp(156), footerTop + dp(12), dp(120), dp(32));
  drawButton(graphics, body, confirm, planText(L"确认", L"Confirm"), true, true);
  addHit(kPlanConfirm, confirm);
  Pen xpen(colorOf(80, 80, 80), 1.6f);
  const float x0 = static_cast<float>(width) - dp(32);
  const float y0 = dp(18);
  graphics.DrawLine(&xpen, x0, y0, x0 + dp(14), y0 + dp(14));
  graphics.DrawLine(&xpen, x0 + dp(14), y0, x0, y0 + dp(14));
  addHit(kPlanClose, RectF(x0 - dp(8), y0 - dp(8), dp(30), dp(30)));
  drawMenu(graphics, body, height);
}

void chooseVoiceFile(HWND window, bool timer) {
  wchar_t file[8192]{};
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = window;
  dialog.lpstrFilter = englishUi() ? L"Audio files (*.mp3;*.wav;*.wma)\0*.mp3;*.wav;*.wma\0\0" : L"音频文件 (*.mp3;*.wav;*.wma)\0*.mp3;*.wav;*.wma\0\0";
  dialog.lpstrFile = file;
  dialog.nMaxFile = 8192;
  const std::wstring directory = alarmDirectory();
  dialog.lpstrInitialDir = directory.c_str();
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
  if (!GetOpenFileNameW(&dialog)) return;
  const std::wstring chosen = file;
  const std::wstring parent = parentDirectory(chosen);
  const std::wstring stored = !parent.empty() && _wcsicmp(parent.c_str(), directory.c_str()) == 0 ? voiceName(chosen) : chosen;
  if (timer) calendarSettings().timerVoice = stored;
  else calendarSettings().alarmVoice = stored;
  persistCalendarSettings();
  InvalidateRect(window, nullptr, FALSE);
}

void previewVoice(const std::wstring& voice) {
  const std::wstring chosen = alarmPath(voice);
  if (!chosen.empty()) {
    playVoiceFile(chosen);
    return;
  }
  if (!voice.empty()) {
    const bool direct = voice.find(L'\\') != std::wstring::npos || voice.find(L'/') != std::wstring::npos;
    logMissingResource(direct ? voice : alarmDirectory() + L"\\" + voice);
    return;
  }
  const std::wstring alarm = randomAlarmPath();
  if (alarm.empty()) logMissingResource(alarmDirectory());
  else playVoiceFile(alarm);
}

HWND makeNumberEdit(HWND parent, int id) {
  HWND edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_CENTER | ES_NUMBER | ES_AUTOHSCROLL, 0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
  SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(planEditFont), TRUE);
  SendMessageW(edit, EM_SETLIMITTEXT, 2, 0);
  return edit;
}

void placeNumberEdit(HWND edit, const EditPlace& place) {
  if (!edit) return;
  if (!place.show) {
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

void writeNumberEdit(HWND edit, int value) {
  if (!edit) return;
  wchar_t text[8];
  std::swprintf(text, 8, L"%d", value);
  wchar_t current[8]{};
  GetWindowTextW(edit, current, 8);
  if (std::wcscmp(current, text) == 0) return;
  syncingEdits = true;
  SetWindowTextW(edit, text);
  syncingEdits = false;
}

void syncTimerEdits() {
  if (hourEdit && GetFocus() != hourEdit) writeNumberEdit(hourEdit, timerHours);
  if (minuteEdit && GetFocus() != minuteEdit) writeNumberEdit(minuteEdit, timerMinutes);
  if (secondEdit && GetFocus() != secondEdit) writeNumberEdit(secondEdit, timerSeconds);
}

void applyTimerEdit(HWND edit, int maxValue, int which) {
  if (!edit || syncingEdits || timerPhase == TimerPhase::Running) return;
  wchar_t text[8]{};
  GetWindowTextW(edit, text, 8);
  if (text[0] == 0) return;
  int value = 0;
  for (const wchar_t* cursor = text; *cursor; cursor += 1) {
    if (*cursor < L'0' || *cursor > L'9') return;
    value = value * 10 + (*cursor - L'0');
  }
  const bool overflow = value > maxValue;
  if (overflow) value = maxValue;
  const int hours = which == 0 ? value : timerHours;
  const int minutes = which == 1 ? value : timerMinutes;
  const int seconds = which == 2 ? value : timerSeconds;
  if (hours != timerHours || minutes != timerMinutes || seconds != timerSeconds) setDuration(hours, minutes, seconds);
  if (overflow) writeNumberEdit(edit, value);
}

void hidePlan(HWND window) {
  openMenu = 0;
  ShowWindow(window, SW_HIDE);
  invalidateCalendar();
}

LRESULT CALLBACK planProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_CREATE:
      loadPreset();
      ensureTimerPump();
      planEditBrush = CreateSolidBrush(RGB(255, 255, 255));
      planEditFont = CreateFontW(-std::max(12, static_cast<int>(dp(14))), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
      hourEdit = makeNumberEdit(window, kEditTimerH);
      minuteEdit = makeNumberEdit(window, kEditTimerM);
      secondEdit = makeNumberEdit(window, kEditTimerS);
      writeNumberEdit(hourEdit, timerHours);
      writeNumberEdit(minuteEdit, timerMinutes);
      writeNumberEdit(secondEdit, timerSeconds);
      SetTimer(window, 1, 200, nullptr);
      return 0;
    case WM_COMMAND: {
      const int id = LOWORD(wParam);
      const int code = HIWORD(wParam);
      if (code == EN_SETFOCUS) {
        openMenu = 0;
        SendMessageW(reinterpret_cast<HWND>(lParam), EM_SETSEL, 0, -1);
      }
      if (code == EN_CHANGE) {
        if (id == kEditTimerH) applyTimerEdit(hourEdit, 23, 0);
        else if (id == kEditTimerM) applyTimerEdit(minuteEdit, 59, 1);
        else if (id == kEditTimerS) applyTimerEdit(secondEdit, 59, 2);
      } else if (code == EN_KILLFOCUS) {
        if (id == kEditTimerH) writeNumberEdit(hourEdit, timerHours);
        else if (id == kEditTimerM) writeNumberEdit(minuteEdit, timerMinutes);
        else if (id == kEditTimerS) writeNumberEdit(secondEdit, timerSeconds);
      }
      if (code == EN_CHANGE || code == EN_SETFOCUS) InvalidateRect(window, nullptr, FALSE);
      return 0;
    }
    case WM_CTLCOLOREDIT:
      SetBkColor(reinterpret_cast<HDC>(wParam), RGB(255, 255, 255));
      SetTextColor(reinterpret_cast<HDC>(wParam), RGB(55, 55, 55));
      return reinterpret_cast<LRESULT>(planEditBrush);
    case WM_SHOWWINDOW:
      if (wParam) InvalidateRect(window, nullptr, FALSE);
      return 0;
    case WM_TIMER:
      tickPlanTimer();
      if (timerPhase == TimerPhase::Running) InvalidateRect(window, nullptr, FALSE);
      return 0;
    case WM_MOUSEWHEEL:
      if (openMenu >= kPlanHour) {
        menuScroll -= GET_WHEEL_DELTA_WPARAM(wParam) / 120;
        InvalidateRect(window, nullptr, FALSE);
      }
      return 0;
    case WM_KEYDOWN:
      if (wParam == VK_ESCAPE) {
        if (openMenu != 0) openMenu = 0;
        else hidePlan(window);
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
      if (client.right > 0 && client.bottom > 0) {
        Gdiplus::Bitmap buffer(client.right, client.bottom, PixelFormat32bppPARGB);
        Graphics graphics(&buffer);
        paintPlan(graphics, client.right, client.bottom);
        Graphics screen(dc);
        screen.DrawImage(&buffer, 0, 0);
      }
      EndPaint(window, &paint);
      placeNumberEdit(hourEdit, hourPlace);
      placeNumberEdit(minuteEdit, minutePlace);
      placeNumberEdit(secondEdit, secondPlace);
      syncTimerEdits();
      return 0;
    }
    case WM_LBUTTONDOWN: {
      const int x = static_cast<short>(LOWORD(lParam));
      const int y = static_cast<short>(HIWORD(lParam));
      const int id = hitTest(x, y);
      if (id >= kPlanMenuItem) {
        applyMenu(id - kPlanMenuItem);
        openMenu = 0;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
      }
      if (id == kPlanMenuBlock) return 0;
      if (openMenu != 0 && id != openMenu) openMenu = 0;
      if (id == kPlanClose || id == kPlanConfirm) {
        hidePlan(window);
        return 0;
      }
      if (id >= kPlanPage && id < kPlanPage + 2) {
        planPage = id - kPlanPage;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
      }
      if (id == 0) {
        InvalidateRect(window, nullptr, FALSE);
        if (y < static_cast<int>(dp(72))) {
          ReleaseCapture();
          SendMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        return 0;
      }
      CalendarSettings& settings = calendarSettings();
      switch (id) {
        case kPlanEnable:
          settings.alarmEnabled = !settings.alarmEnabled;
          persistCalendarSettings();
          break;
        case kPlanHour:
        case kPlanMinute:
        case kPlanVoice:
        case kPlanTimerH:
        case kPlanTimerM:
        case kPlanTimerS:
        case kPlanTimerVoice:
          openList(id);
          break;
        case kPlanBrowse:
          chooseVoiceFile(window, false);
          break;
        case kPlanPreview:
          previewVoice(settings.alarmVoice);
          break;
        case kPlanTimerBrowse:
          chooseVoiceFile(window, true);
          break;
        case kPlanTimerPreview:
          previewVoice(settings.timerVoice);
          break;
        case kPlanStart:
          startTimer();
          break;
        case kPlanPause:
          pauseTimer();
          break;
        case kPlanReset:
          resetTimer();
          break;
        case kPlanPreset:
        case kPlanPreset + 1:
        case kPlanPreset + 2:
        case kPlanPreset + 3: {
          const int minutes[] = {1, 5, 10, 25};
          setDuration(0, minutes[id - kPlanPreset], 0);
          break;
        }
        default:
          break;
      }
      InvalidateRect(window, nullptr, FALSE);
      return 0;
    }
    case WM_CLOSE:
      hidePlan(window);
      return 0;
    case WM_DESTROY:
      KillTimer(window, 1);
      if (planEditFont) DeleteObject(planEditFont);
      if (planEditBrush) DeleteObject(planEditBrush);
      planEditFont = nullptr;
      planEditBrush = nullptr;
      hourEdit = nullptr;
      minuteEdit = nullptr;
      secondEdit = nullptr;
      planWindow = nullptr;
      invalidateCalendar();
      return 0;
    default:
      return DefWindowProcW(window, message, wParam, lParam);
  }
}

}

void pollVoiceClock() {
  ensureTimerPump();
  tickPlanTimer();
  SYSTEMTIME now{};
  GetLocalTime(&now);
  const int minuteKey = ((now.wYear * 100 + now.wMonth) * 100 + now.wDay) * 10000 + now.wHour * 100 + now.wMinute;
  static int startedMinute = -1;
  static int announcedMinute = -1;
  static int alarmDay = -1;
  if (startedMinute < 0) {
    startedMinute = minuteKey;
    announcedMinute = minuteKey;
  }
  const CalendarSettings settings = calendarSettings();
  const int dayKey = (now.wYear * 100 + now.wMonth) * 100 + now.wDay;
  const bool alarmDue = settings.alarmEnabled && now.wHour == settings.alarmHour && now.wMinute == settings.alarmMinute && alarmDay != dayKey && now.wSecond < 20;
  if (alarmDue) {
    const std::wstring chosen = alarmPath(settings.alarmVoice);
    const std::wstring alarm = chosen.empty() ? randomAlarmPath() : chosen;
    if (alarm.empty()) logMissingResource(alarmDirectory());
    else playVoiceFile(alarm);
    wchar_t when[32];
    std::swprintf(when, 32, L"%02d:%02d", settings.alarmHour, settings.alarmMinute);
    showDuePopup(planText(L"闹钟", L"Alarm"), when, L"Alarm.gif");
    alarmDay = dayKey;
    announcedMinute = minuteKey;
    return;
  }
  if (!settings.timeAnnounce || now.wSecond > 1 || minuteKey == announcedMinute) return;
  if (!announcementDue(settings.announceMode, now.wMinute) || quietNow(settings, now) || blockedByFullscreen(settings)) {
    announcedMinute = minuteKey;
    return;
  }
  const std::wstring path = announcementPath(settings.announceMode, now.wHour, now.wMinute);
  if (path.empty()) {
    const std::wstring root = soundsRoot();
    const DWORD attributes = GetFileAttributesW(root.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) logMissingResource(root);
    else if (settings.announceMode == 3) logMissingResource(root + L"\\" + clockName(now.wHour, now.wMinute));
    else logMissingResource(root + L"\\Serif\\" + clockName(now.wHour, now.wMinute));
  } else playVoiceFile(path);
  announcedMinute = minuteKey;
}

bool planWindowVisible() {
  return planWindow && IsWindowVisible(planWindow);
}

void refreshPlanWindow() {
  if (!planWindow) return;
  SetWindowTextW(planWindow, englishUi() ? L"Plans" : L"计划");
  InvalidateRect(planWindow, nullptr, FALSE);
}

void applyPlanWindowSize() {
  if (!planWindow) return;
  const FittedWindow fitted = fitDesign(kPanelWidth, kPanelHeight);
  planScale = fitted.scale;
  moveFittedWindow(planWindow, fitted);
}

void showPlanWindow(HWND owner) {
  if (planWindow) {
    applyPlanWindowSize();
    refreshPlanWindow();
    ShowWindow(planWindow, SW_SHOW);
    SetWindowPos(planWindow, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(planWindow);
    invalidateCalendar();
    return;
  }
  WNDCLASSW windowClass{};
  windowClass.lpfnWndProc = planProc;
  windowClass.hInstance = GetModuleHandleW(nullptr);
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.lpszClassName = L"YxCalendarPlan";
  RegisterClassW(&windowClass);
  const FittedWindow fitted = fitDesign(kPanelWidth, kPanelHeight);
  planScale = fitted.scale;
  const int width = fitted.width;
  const int height = fitted.height;
  RECT work{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  RECT parent{};
  if (owner) GetWindowRect(owner, &parent);
  int x = static_cast<int>(parent.right) + 12;
  int y = static_cast<int>(parent.top) + 40;
  if (x + width > work.right) x = std::max(static_cast<int>(work.left), static_cast<int>(parent.left) - width - 12);
  if (x < work.left) x = static_cast<int>(work.left);
  if (y + height > work.bottom) y = std::max(static_cast<int>(work.top), static_cast<int>(work.bottom) - height - 8);
  planWindow = CreateWindowExW(WS_EX_TOOLWINDOW, windowClass.lpszClassName, englishUi() ? L"Plans" : L"计划", WS_POPUP | WS_CLIPCHILDREN, x, y, width, height, owner, nullptr, windowClass.hInstance, nullptr);
  if (!planWindow) return;
  ShowWindow(planWindow, SW_SHOW);
  invalidateCalendar();
}
