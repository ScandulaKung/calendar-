#pragma once

#include <string>

struct LunarDate {
  int year = 0;
  int month = 0;
  int day = 0;
  bool leap = false;
};

LunarDate lunarFromGregorian(int year, int month, int day);
std::wstring lunarMonthName(const LunarDate& date);
std::wstring lunarMonthDayText(const LunarDate& date);
struct DayLabel {
  std::wstring text;
  bool highlighted = false;
};

DayLabel dayLabel(int year, int month, int day);
DayLabel religiousLabel(int year, int month, int day);
std::wstring listedFestival(int year, int month, int day, bool religious);
std::wstring daySubtitle(int year, int month, int day);
int solarTermDay(int year, int index);
