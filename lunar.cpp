#include "lunar.hpp"

#include "date_calc.hpp"

#include <cmath>

namespace {

constexpr unsigned kLunarInfo[] = {
    0x04bd8, 0x04ae0, 0x0a570, 0x054d5, 0x0d260, 0x0d950, 0x16554, 0x056a0, 0x09ad0, 0x055d2,
    0x04ae0, 0x0a5b6, 0x0a4d0, 0x0d250, 0x1d255, 0x0b540, 0x0d6a0, 0x0ada2, 0x095b0, 0x14977,
    0x04970, 0x0a4b0, 0x0b4b5, 0x06a50, 0x06d40, 0x1ab54, 0x02b60, 0x09570, 0x052f2, 0x04970,
    0x06566, 0x0d4a0, 0x0ea50, 0x06e95, 0x05ad0, 0x02b60, 0x186e3, 0x092e0, 0x1c8d7, 0x0c950,
    0x0d4a0, 0x1d8a6, 0x0b550, 0x056a0, 0x1a5b4, 0x025d0, 0x092d0, 0x0d2b2, 0x0a950, 0x0b557,
    0x06ca0, 0x0b550, 0x15355, 0x04da0, 0x0a5b0, 0x14573, 0x052b0, 0x0a9a8, 0x0e950, 0x06aa0,
    0x0aea6, 0x0ab50, 0x04b60, 0x0aae4, 0x0a570, 0x05260, 0x0f263, 0x0d950, 0x05b57, 0x056a0,
    0x096d0, 0x04dd5, 0x04ad0, 0x0a4d0, 0x0d4d4, 0x0d250, 0x0d558, 0x0b540, 0x0b6a0, 0x195a6,
    0x095b0, 0x049b0, 0x0a974, 0x0a4b0, 0x0b27a, 0x06a50, 0x06d40, 0x0af46, 0x0ab60, 0x09570,
    0x04af5, 0x04970, 0x064b0, 0x074a3, 0x0ea50, 0x06b58, 0x055c0, 0x0ab60, 0x096d5, 0x092e0,
    0x0c960, 0x0d954, 0x0d4a0, 0x0da50, 0x07552, 0x056a0, 0x0abb7, 0x025d0, 0x092d0, 0x0cab5,
    0x0a950, 0x0b4a0, 0x0baa4, 0x0ad50, 0x055d9, 0x04ba0, 0x0a5b0, 0x15176, 0x052b0, 0x0a930,
    0x07954, 0x06aa0, 0x0ad50, 0x05b52, 0x04b60, 0x0a6e6, 0x0a4e0, 0x0d260, 0x0ea65, 0x0d530,
    0x05aa0, 0x076a3, 0x096d0, 0x04afb, 0x04ad0, 0x0a4d0, 0x1d0b6, 0x0d250, 0x0d520, 0x0dd45,
    0x0b5a0, 0x056d0, 0x055b2, 0x049b0, 0x0a577, 0x0a4b0, 0x0aa50, 0x1b255, 0x06d20, 0x0ada0,
    0x14b63, 0x09370, 0x049f8, 0x04970, 0x064b0, 0x168a6, 0x0ea50, 0x06b20, 0x1a6c4, 0x0aae0,
    0x0a2e0, 0x0d2e3, 0x0c960, 0x0d557, 0x0d4a0, 0x0da50, 0x05d55, 0x056a0, 0x0a6d0, 0x055d4,
    0x052d0, 0x0a9b8, 0x0a950, 0x0b4a0, 0x0b6a6, 0x0ad50, 0x055a0, 0x0aba4, 0x0a5b0, 0x052b0,
    0x0b273, 0x06930, 0x07337, 0x06aa0, 0x0ad50, 0x14b55, 0x04b60, 0x0a570, 0x054e4, 0x0d160,
    0x0e968, 0x0d520, 0x0daa0, 0x16aa6, 0x056d0, 0x04ae0, 0x0a9d4, 0x0a2d0, 0x0d150, 0x0f252,
    0x0d520};

constexpr int kTermInfo[] = {
    0, 21208, 42467, 63836, 85337, 107014, 128867, 150921, 173149, 195551, 218072, 240693,
    263343, 285989, 308563, 331033, 353350, 375494, 397447, 419210, 440795, 462224, 483532, 504758};

const wchar_t* kMonthNames[] = {L"", L"正", L"二", L"三", L"四", L"五", L"六", L"七", L"八", L"九", L"十", L"冬", L"腊"};
const wchar_t* kTermNames[] = {
    L"小寒", L"大寒", L"立春", L"雨水", L"惊蛰", L"春分", L"清明", L"谷雨", L"立夏", L"小满", L"芒种", L"夏至",
    L"小暑", L"大暑", L"立秋", L"处暑", L"白露", L"秋分", L"寒露", L"霜降", L"立冬", L"小雪", L"大雪", L"冬至"};

int leapMonth(int year) {
  return static_cast<int>(kLunarInfo[year - 1900] & 0xf);
}

int leapDays(int year) {
  if (leapMonth(year) == 0) return 0;
  return (kLunarInfo[year - 1900] & 0x10000) ? 30 : 29;
}

int monthDays(int year, int month) {
  return (kLunarInfo[year - 1900] & (0x10000u >> month)) ? 30 : 29;
}

int yearDays(int year) {
  int sum = 348;
  for (unsigned mask = 0x8000; mask > 0x8; mask >>= 1) sum += (kLunarInfo[year - 1900] & mask) ? 1 : 0;
  return sum + leapDays(year);
}

long long civilDays(int year, int month, int day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return static_cast<long long>(era) * 146097 + static_cast<long long>(dayOfEra) - 719468;
}

void civilFromDays(long long z, int& year, int& month, int& day) {
  z += 719468;
  const long long era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned dayOfEra = static_cast<unsigned>(z - era * 146097);
  const unsigned yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
  year = static_cast<int>(yearOfEra) + static_cast<int>(era * 400);
  const unsigned dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
  const unsigned monthPart = (5 * dayOfYear + 2) / 153;
  day = static_cast<int>(dayOfYear - (153 * monthPart + 2) / 5 + 1);
  month = static_cast<int>(monthPart < 10 ? monthPart + 3 : monthPart - 9);
  year += month <= 2;
}

std::wstring lunarDayText(int day) {
  static const wchar_t* ones[] = {L"", L"一", L"二", L"三", L"四", L"五", L"六", L"七", L"八", L"九"};
  if (day == 10) return L"初十";
  if (day == 20) return L"二十";
  if (day == 30) return L"三十";
  if (day < 10) return std::wstring(L"初") + ones[day];
  if (day < 20) return std::wstring(L"十") + ones[day - 10];
  return std::wstring(L"廿") + ones[day - 20];
}

void easterSunday(int year, int& month, int& day) {
  const int a = year % 19;
  const int b = year / 100;
  const int c = year % 100;
  const int d = b / 4;
  const int e = b % 4;
  const int f = (b + 8) / 25;
  const int g = (b - f + 1) / 3;
  const int h = (19 * a + b - d - g + 15) % 30;
  const int i = c / 4;
  const int k = c % 4;
  const int l = (32 + 2 * e + 2 * i - h - k) % 7;
  const int m = (a + 11 * h + 22 * l) / 451;
  month = (h + l - 7 * m + 114) / 31;
  day = (h + l - 7 * m + 114) % 31 + 1;
}

const wchar_t* gregorianFestival(int month, int day) {
  struct NamedDay { int month; int day; const wchar_t* name; };
  static const NamedDay days[] = {
      {1, 1, L"元旦"}, {1, 10, L"警察节"}, {2, 7, L"二七纪念"}, {2, 14, L"情人节"},
      {3, 8, L"妇女节"}, {3, 12, L"植树节"}, {3, 15, L"消费者日"}, {4, 1, L"愚人节"},
      {4, 15, L"安全日"}, {4, 24, L"航天日"}, {5, 1, L"劳动节"}, {5, 4, L"青年节"},
      {5, 10, L"品牌日"}, {5, 12, L"护士节"}, {5, 30, L"科技日"}, {6, 1, L"儿童节"},
      {7, 1, L"建党节"}, {7, 7, L"七七抗战"}, {8, 1, L"建军节"}, {8, 8, L"健身日"},
      {8, 19, L"医师节"}, {9, 3, L"抗战胜利"}, {9, 10, L"教师节"}, {9, 18, L"九一八"},
      {9, 30, L"烈士日"}, {10, 1, L"国庆节"}, {10, 17, L"扶贫日"}, {11, 8, L"记者节"},
      {11, 9, L"消防日"}, {12, 4, L"宪法日"}, {12, 13, L"国家公祭"}, {12, 25, L"圣诞节"},
  };
  for (const NamedDay& item : days) {
    if (item.month == month && item.day == day) return item.name;
  }
  return nullptr;
}

const wchar_t* nthWeekdayName(int year, int month, int day, int targetMonth, int weekday, int nth, const wchar_t* name) {
  if (month != targetMonth) return nullptr;
  const int start = weekdayIndex(year, month, 1);
  if (start < 0) return nullptr;
  const int date = 1 + (weekday - start + 7) % 7 + (nth - 1) * 7;
  return day == date ? name : nullptr;
}

const wchar_t* movableMemorial(int year, int month, int day) {
  if (const wchar_t* name = nthWeekdayName(year, month, day, 5, 0, 3, L"助残日")) return name;
  if (const wchar_t* name = nthWeekdayName(year, month, day, 6, 6, 2, L"遗产日")) return name;
  if (const wchar_t* name = nthWeekdayName(year, month, day, 9, 6, 3, L"国防教育")) return name;
  return nullptr;
}

const wchar_t* lunarFestival(const LunarDate& lunar) {
  if (lunar.leap) return nullptr;
  if (lunar.month == 1 && lunar.day == 1) return L"春节";
  if (lunar.month == 1 && lunar.day == 15) return L"元宵节";
  if (lunar.month == 2 && lunar.day == 2) return L"龙抬头";
  if (lunar.month == 5 && lunar.day == 5) return L"端午节";
  if (lunar.month == 7 && lunar.day == 7) return L"七夕";
  if (lunar.month == 7 && lunar.day == 15) return L"中元节";
  if (lunar.month == 8 && lunar.day == 15) return L"中秋节";
  if (lunar.month == 9 && lunar.day == 9) return L"重阳节";
  if (lunar.month == 12 && lunar.day == 8) return L"腊八节";
  const int lastDay = monthDays(lunar.year, 12);
  if (lunar.month == 12 && lunar.day == lastDay) return L"除夕";
  return nullptr;
}

}

LunarDate lunarFromGregorian(int year, int month, int day) {
  LunarDate result;
  if (year < 1900 || year > 2100) return result;
  int offset = static_cast<int>(civilDays(year, month, day) - civilDays(1900, 1, 31));
  int lunarYear = 1900;
  int yearLength = 0;
  for (; lunarYear < 2101 && offset > 0; lunarYear += 1) {
    yearLength = yearDays(lunarYear);
    offset -= yearLength;
  }
  if (offset < 0) {
    offset += yearLength;
    lunarYear -= 1;
  }
  const int leap = leapMonth(lunarYear);
  bool inLeap = false;
  int lunarMonth = 1;
  int length = 0;
  for (; lunarMonth < 13 && offset > 0; lunarMonth += 1) {
    if (leap > 0 && lunarMonth == leap + 1 && !inLeap) {
      lunarMonth -= 1;
      inLeap = true;
      length = leapDays(lunarYear);
    } else {
      length = monthDays(lunarYear, lunarMonth);
    }
    if (inLeap && lunarMonth == leap + 1) inLeap = false;
    offset -= length;
  }
  if (offset == 0 && leap > 0 && lunarMonth == leap + 1) {
    if (inLeap) inLeap = false;
    else {
      inLeap = true;
      lunarMonth -= 1;
    }
  }
  if (offset < 0) {
    offset += length;
    lunarMonth -= 1;
  }
  result.year = lunarYear;
  result.month = lunarMonth;
  result.day = offset + 1;
  result.leap = inLeap;
  return result;
}

std::wstring lunarMonthName(const LunarDate& date) {
  if (date.month < 1 || date.month > 12) return L"";
  std::wstring text = date.leap ? L"闰" : L"";
  text += kMonthNames[date.month];
  text += L"月";
  return text;
}

std::wstring lunarMonthDayText(const LunarDate& date) {
  return lunarMonthName(date) + lunarDayText(date.day);
}

int solarTermDay(int year, int index) {
  const double base = -25562.0 * 86400000.0 + (2 * 60 + 5) * 60000.0;
  const double millis = 31556925974.7 * (year - 1900) + kTermInfo[index] * 60000.0 + base;
  const long long days = static_cast<long long>(std::floor(millis / 86400000.0));
  int termYear = 0;
  int termMonth = 0;
  int termDay = 0;
  civilFromDays(days, termYear, termMonth, termDay);
  return termDay;
}

DayLabel dayLabel(int year, int month, int day) {
  DayLabel label;
  const LunarDate lunar = lunarFromGregorian(year, month, day);
  if (const wchar_t* festival = gregorianFestival(month, day)) {
    label.text = festival;
    label.highlighted = true;
    return label;
  }
  if (const wchar_t* memorial = movableMemorial(year, month, day)) {
    label.text = memorial;
    label.highlighted = true;
    return label;
  }
  int easterMonth = 0;
  int easterDay = 0;
  easterSunday(year, easterMonth, easterDay);
  if (month == easterMonth && day == easterDay) {
    label.text = L"复活节";
    label.highlighted = true;
    return label;
  }
  for (int index = 0; index < 24; index += 1) {
    if (index / 2 + 1 == month && solarTermDay(year, index) == day) {
      const wchar_t* term = kTermNames[index];
      if (std::wstring(term) == L"清明") term = L"清明节";
      else if (std::wstring(term) == L"秋分") term = L"丰收节";
      label.text = term;
      label.highlighted = true;
      return label;
    }
  }
  if (lunar.day == 1) {
    label.text = lunarMonthName(lunar);
    label.highlighted = true;
    return label;
  }
  if (const wchar_t* festival = lunarFestival(lunar)) {
    label.text = festival;
    label.highlighted = true;
    return label;
  }
  label.text = lunarDayText(lunar.day);
  return label;
}

std::wstring daySubtitle(int year, int month, int day) {
  return dayLabel(year, month, day).text;
}

std::wstring listedFestival(int year, int month, int day, bool religious) {
  if (religious) {
    const DayLabel label = religiousLabel(year, month, day);
    return label.highlighted ? label.text : L"";
  }
  if (const wchar_t* name = gregorianFestival(month, day)) return name;
  if (const wchar_t* name = movableMemorial(year, month, day)) return name;
  int easterMonth = 0;
  int easterDay = 0;
  easterSunday(year, easterMonth, easterDay);
  if (month == easterMonth && day == easterDay) return L"复活节";
  for (int index = 0; index < 24; index += 1) {
    if (index / 2 + 1 != month || solarTermDay(year, index) != day) continue;
    const std::wstring term = kTermNames[index];
    if (term == L"清明") return L"清明节";
    if (term == L"秋分") return L"丰收节";
  }
  const LunarDate lunar = lunarFromGregorian(year, month, day);
  if (const wchar_t* name = lunarFestival(lunar)) return name;
  return L"";
}

DayLabel religiousLabel(int year, int month, int day) {
  DayLabel label;
  int easterMonth = 0;
  int easterDay = 0;
  easterSunday(year, easterMonth, easterDay);
  const auto moved = [&](int delta) {
    int outYear = 0;
    int outMonth = 0;
    int outDay = 0;
    civilFromDays(civilDays(year, easterMonth, easterDay) + delta, outYear, outMonth, outDay);
    return outYear == year && outMonth == month && outDay == day;
  };
  const wchar_t* movable = nullptr;
  if (moved(-46)) movable = L"Ash Wed.";
  else if (moved(-7)) movable = L"Palm Sun.";
  else if (moved(-2)) movable = L"Good Fri.";
  else if (month == easterMonth && day == easterDay) movable = L"Easter";
  else if (moved(39)) movable = L"Ascension";
  else if (moved(49)) movable = L"Pentecost";
  else if (moved(56)) movable = L"Trinity";
  else if (moved(60)) movable = L"Corpus";
  if (!movable) {
    const int start = weekdayIndex(year, 11, 27);
    if (start >= 0) {
      int adventMonth = 11;
      int adventDay = 27 + (7 - start) % 7;
      if (adventDay > 30) {
        adventMonth = 12;
        adventDay -= 30;
      }
      if (month == adventMonth && day == adventDay) movable = L"Advent";
    }
  }
  if (movable) {
    label.text = movable;
    label.highlighted = true;
    return label;
  }
  struct NamedDay { int month; int day; const wchar_t* name; };
  static const NamedDay days[] = {
      {1, 6, L"Epiphany"}, {2, 2, L"Candlemas"}, {3, 17, L"Patrick"}, {3, 19, L"St Joseph"},
      {3, 25, L"Annunciation"}, {6, 24, L"St John"}, {6, 29, L"Peter Paul"}, {8, 6, L"Transfigure"},
      {8, 15, L"Assumption"}, {9, 8, L"Mary's Birth"}, {9, 14, L"Holy Cross"}, {9, 29, L"Michaelmas"},
      {11, 1, L"All Saints"}, {11, 2, L"All Souls"}, {12, 6, L"Nicholas"}, {12, 8, L"Conception"},
      {12, 24, L"Xmas Eve"}, {12, 25, L"Christmas"}, {12, 26, L"St Stephen"}, {12, 28, L"Innocents"},
  };
  for (const NamedDay& item : days) {
    if (item.month == month && item.day == day) {
      label.text = item.name;
      label.highlighted = true;
      return label;
    }
  }
  return label;
}
