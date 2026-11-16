#pragma once

// 优效日历 2.5.1.13 日期计算。
// YXCalendar.exe SHA-256 3072b3d1f4566c6a61232a75bc1565b1336fbb5ee6795c16492934b13f3aac5f

enum class DayTag { None, Rest, Makeup };

struct CalendarDate {
  int year = 0;
  int month = 0;
  int day = 0;
  int weekday = 0;
  DayTag tag = DayTag::None;
};

struct DateRangeResult {
  bool ok = false;
  int span = 0;
  int inclusiveDays = 0;
  int workdays = 0;
  int weekends = 0;
  int holidays = 0;
  int makeupDays = 0;
  CalendarDate earlier{};
  CalendarDate later{};
};

bool isLeapYear(int year);
int daysInMonth(int year, int month);
bool isValidDate(int year, int month, int day);
int dayOfYear(int year, int month, int day);
int daySpan(int startYear, int startMonth, int startDay, int endYear, int endMonth, int endDay);
int weekdayIndex(int year, int month, int day);
DateRangeResult calculateDateRange(int startYear, int startMonth, int startDay, int endYear, int endMonth, int endDay);
