#include "date_calc.hpp"

#include <cstdlib>
#include <iterator>

namespace {

int dateKey(int year, int month, int day) {
  return year * 10000 + month * 100 + day;
}

bool contains(const int* first, const int* last, int key) {
  while (first < last) {
    const int* middle = first + (last - first) / 2;
    if (*middle == key) return true;
    if (*middle < key) first = middle + 1;
    else last = middle;
  }
  return false;
}

const int kRestDays[] = {
    20240101, 20240210, 20240211, 20240212, 20240213, 20240214, 20240215, 20240216, 20240217,
    20240404, 20240405, 20240406, 20240501, 20240502, 20240503, 20240504, 20240505, 20240608,
    20240609, 20240610, 20240915, 20240916, 20240917, 20241001, 20241002, 20241003, 20241004,
    20241005, 20241006, 20241007, 20250101, 20250128, 20250129, 20250130, 20250131, 20250201,
    20250202, 20250203, 20250204, 20250404, 20250405, 20250406, 20250501, 20250502, 20250503,
    20250504, 20250505, 20250531, 20250601, 20250602, 20251001, 20251002, 20251003, 20251004,
    20251005, 20251006, 20251007, 20251008, 20260101, 20260102, 20260103, 20260215, 20260216,
    20260217, 20260218, 20260219, 20260220, 20260221, 20260222, 20260223, 20260404, 20260405,
    20260406, 20260501, 20260502, 20260503, 20260504, 20260505, 20260619, 20260620, 20260621,
    20260925, 20260926, 20260927, 20261001, 20261002, 20261003, 20261004, 20261005, 20261006,
    20261007,
};

const int kMakeupDays[] = {
    20240204, 20240218, 20240407, 20240428, 20240511, 20240914, 20240929, 20241012,
    20250126, 20250208, 20250427, 20250928, 20251011, 20260104, 20260214, 20260228, 20260509,
    20260920, 20261010,
};

DayTag dayTag(int year, int month, int day) {
  const int key = dateKey(year, month, day);
  if (contains(std::begin(kRestDays), std::end(kRestDays), key)) return DayTag::Rest;
  if (contains(std::begin(kMakeupDays), std::end(kMakeupDays), key)) return DayTag::Makeup;
  return DayTag::None;
}

void nextDay(int& year, int& month, int& day) {
  if (day < daysInMonth(year, month)) {
    day += 1;
    return;
  }
  day = 1;
  if (month < 12) {
    month += 1;
    return;
  }
  month = 1;
  year += 1;
}

CalendarDate describe(int year, int month, int day) {
  CalendarDate date;
  date.year = year;
  date.month = month;
  date.day = day;
  date.weekday = weekdayIndex(year, month, day);
  date.tag = dayTag(year, month, day);
  return date;
}

}

bool isLeapYear(int year) {
  const bool divisibleBy4 = year % 4 == 0;
  const bool divisibleBy100 = year % 100 == 0;
  const bool divisibleBy400 = year % 400 == 0;
  if (divisibleBy4 && !divisibleBy100) return true;
  return divisibleBy400;
}

int daysInMonth(int year, int month) {
  if (month == 2) return 28 + (isLeapYear(year) ? 1 : 0);
  if (month == 4 || month == 6 || month == 9 || month == 11) return 30;
  return 31;
}

bool isValidDate(int year, int month, int day) {
  if (year - 1900 > 200 || year - 1900 < 0) return false;
  if (month - 1 > 11 || month - 1 < 0) return false;
  if (day < 1) return false;
  return day <= daysInMonth(year, month);
}

int dayOfYear(int year, int month, int day) {
  if (!isValidDate(year, month, day)) return 0;
  int total = 0;
  for (int current = 1; current < month; current += 1) total += daysInMonth(year, current);
  return total + day - 1;
}

int daySpan(int startYear, int startMonth, int startDay, int endYear, int endMonth, int endDay) {
  if (!isValidDate(startYear, startMonth, startDay) || !isValidDate(endYear, endMonth, endDay)) return -1;
  if (startYear < endYear) {
    int middle = 0;
    for (int year = startYear + 1; year < endYear; year += 1) middle += 365 + (isLeapYear(year) ? 1 : 0);
    const int startLength = 365 + (isLeapYear(startYear) ? 1 : 0);
    return startLength - dayOfYear(startYear, startMonth, startDay) + middle + dayOfYear(endYear, endMonth, endDay);
  }
  if (startYear > endYear) return -daySpan(endYear, endMonth, endDay, startYear, startMonth, startDay);
  if (startMonth < endMonth) {
    int between = 0;
    for (int month = startMonth + 1; month < endMonth; month += 1) between += daysInMonth(startYear, month);
    return daysInMonth(startYear, startMonth) - startDay + between + endDay;
  }
  if (startMonth > endMonth) return -daySpan(endYear, endMonth, endDay, startYear, startMonth, startDay);
  if (startDay <= endDay) return endDay - startDay;
  return -daySpan(endYear, endMonth, endDay, startYear, startMonth, startDay);
}

int weekdayIndex(int year, int month, int day) {
  if (!isValidDate(year, month, day)) return -1;
  int adjustedYear = year;
  int adjustedMonth = month;
  if (adjustedMonth == 1 || adjustedMonth == 2) {
    adjustedYear -= 1;
    adjustedMonth += 12;
  }
  const int century = adjustedYear / 100;
  const int yearOfCentury = adjustedYear - century * 100;
  int accumulated = (26 * (adjustedMonth + 1)) / 10;
  accumulated += yearOfCentury / 4;
  accumulated += century / 4;
  accumulated -= century * 2;
  accumulated += yearOfCentury;
  int index = day - 1 + accumulated;
  index %= 7;
  if (index < 0) index += 7;
  return index;
}

DateRangeResult calculateDateRange(int startYear, int startMonth, int startDay, int endYear, int endMonth, int endDay) {
  DateRangeResult result;
  const int span = daySpan(startYear, startMonth, startDay, endYear, endMonth, endDay);
  if (span == -1) return result;
  const bool forward = span >= 0;
  int year = forward ? startYear : endYear;
  int month = forward ? startMonth : endMonth;
  int day = forward ? startDay : endDay;
  result.earlier = describe(year, month, day);
  const int steps = std::abs(span) + 1;
  for (int remaining = steps; remaining > 0; remaining -= 1) {
    const DayTag tag = dayTag(year, month, day);
    if (tag == DayTag::Rest) result.holidays += 1;
    else if (tag == DayTag::Makeup) result.makeupDays += 1;
    const int weekday = weekdayIndex(year, month, day);
    if (weekday == 6 || weekday == 0) {
      result.weekends += 1;
      if (tag == DayTag::Makeup) result.workdays += 1;
    } else if (tag != DayTag::Rest) {
      result.workdays += 1;
    }
    nextDay(year, month, day);
  }
  result.later = describe(forward ? endYear : startYear, forward ? endMonth : startMonth, forward ? endDay : startDay);
  result.ok = true;
  result.span = std::abs(span);
  result.inclusiveDays = steps;
  return result;
}
