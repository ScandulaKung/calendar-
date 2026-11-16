#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

struct CalendarSettings {
  bool launchAtStartup = true;
  bool hour24 = true;
  bool zodiacFromLichun = true;
  bool weekStartsMonday = true;
  bool showWeatherFloat = true;
  std::wstring hoverSeconds = L"0.2";
  bool weatherAlert = false;
  int clockAlign = 1;
  bool dualScreen = false;
  std::wstring formatPrimary = L"mm-dd HH:ff\\nMD ddd";
  std::wstring formatSecondary = L"icnwe1\\ntl1~th1";
  std::wstring switchPrimary = L"5";
  std::wstring switchSecondary = L"3";
  bool switchUpward = true;
  std::wstring clockFont = L"Microsoft YaHei";
  int clockFontSize = 0;
  bool clockFontBold = false;
  bool clockFontItalic = false;
  std::wstring dockFont;
  int dockFontSize = 0;
  bool dockFontBold = false;
  bool dockFontItalic = false;
  bool timeAnnounce = false;
  bool announceHourly = true;
  int announceMode = 0;
  bool weatherAnnounce = false;
  bool quietMode = true;
  int quietFrom = 23;
  int quietTo = 6;
  bool soundInFullscreen = false;
  bool alarmEnabled = false;
  int alarmHour = 7;
  int alarmMinute = 0;
  std::wstring alarmVoice;
  int timerHours = 0;
  int timerMinutes = 5;
  int timerSeconds = 0;
  std::wstring timerVoice;
  int syncMode = 0;
  int customYear = 2026;
  int customMonth = 11;
  int customDay = 7;
  int customHour = 3;
  int customMinute = 47;
  int customSecond = 26;
  bool receivePush = true;
  int dpiMode = 0;
  int appearance = 0;
  int language = 0;
  int windowOpacity = 100;
  bool dockClock = false;
  bool backgroundShuffle = false;
  bool backgroundOrder = false;
  int backgroundInterval = 10;
  long long backgroundOrderStart = 0;
  int backgroundCrop = 1;
  std::wstring backgroundImage;
  std::wstring qweatherHost;
  std::wstring qweatherKid;
  std::wstring qweatherIssuer;
  std::wstring qweatherProject;
  std::wstring qweatherPrivate;
  bool qweatherSkip = false;
  std::wstring status;
};

inline int backgroundIntervalMinutes(int minutes) {
  const int choices[] = {1, 5, 10, 30, 60, 180, 480};
  for (int choice : choices) {
    if (choice == minutes) return choice;
  }
  return 10;
}

constexpr float kPanelWidth = 740.f;
constexpr float kPanelHeight = 560.f;
constexpr int kBaseScreenWidth = 2048;
constexpr int kBaseScreenHeight = 1152;

struct FittedWindow {
  int width = 0;
  int height = 0;
  float scale = 1.f;
};

FittedWindow fitDesign(float designWidth, float designHeight);
void moveFittedWindow(HWND window, const FittedWindow& fitted);
void applySettingsWindowSize();

void promptQWeatherCredentials(HWND owner, bool force);

CalendarSettings& calendarSettings();
inline bool englishUi() { return calendarSettings().language == 1; }
void persistCalendarSettings();
void showSettingsWindow(HWND owner);
bool settingsWindowVisible();
void invalidateCalendar();
std::wstring activeClockText();
LOGFONTW styledLogFont(const std::wstring& face, float pixels, bool bold, bool italic);
