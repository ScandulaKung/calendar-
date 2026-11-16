#include "almanac.hpp"

#include "date_calc.hpp"
#include "lunar.hpp"
#include "settings.hpp"

namespace {

const wchar_t* stems[] = {L"甲", L"乙", L"丙", L"丁", L"戊", L"己", L"庚", L"辛", L"壬", L"癸"};
const wchar_t* branches[] = {L"子", L"丑", L"寅", L"卯", L"辰", L"巳", L"午", L"未", L"申", L"酉", L"戌", L"亥"};
const wchar_t* suitableByDuty[] = {
    L"祈福 求嗣 祭祀", L"沐浴 扫舍 理发", L"祈福 嫁娶 开市", L"修造 涂泥 平路",
    L"嫁娶 开市 安床", L"祭祀 捕捉 牧养", L"破屋 坏垣", L"祭祀 安床 纳财",
    L"开市 入学 嫁娶", L"纳财 捕捉 收账", L"开市 求财 出行", L"安葬 修坟 塞穴",
};
const wchar_t* avoidByDuty[] = {
    L"开仓 动土 出行", L"嫁娶 开市", L"服药 栽种", L"祈福 求嗣",
    L"诉讼 出行", L"开市 嫁娶", L"嫁娶 签约", L"登高 乘船",
    L"诉讼 词讼", L"开仓 出行", L"安葬 修坟", L"开市 出行",
};

int julianDay(int year, int month, int day) {
  const int a = (14 - month) / 12;
  const int y = year + 4800 - a;
  const int m = month + 12 * a - 3;
  return day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
}

int positiveMod(int value, int divisor) {
  int result = value % divisor;
  if (result < 0) result += divisor;
  return result;
}

bool onOrAfterTerm(int year, int month, int day, int termIndex, int termMonth) {
  const int termDay = solarTermDay(year, termIndex);
  return month > termMonth || (month == termMonth && day >= termDay);
}

int monthBranch(int year, int month, int day) {
  const int termIndex[] = {0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22};
  const int termMonth[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  const int branch[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0};
  int current = 0;
  for (int index = 0; index < 12; index += 1) {
    if (onOrAfterTerm(year, month, day, termIndex[index], termMonth[index])) current = branch[index];
  }
  return current;
}

int pillarYear(int year, int month, int day) {
  if (calendarSettings().zodiacFromLichun) {
    if (!onOrAfterTerm(year, month, day, 2, 2)) return year - 1;
    return year;
  }
  return lunarFromGregorian(year, month, day).year;
}

}

Almanac zodiacFor(int month, int day) {
  struct Sign {
    int startMonth;
    int startDay;
    const wchar_t* name;
    const wchar_t* span;
    const wchar_t* element;
    const wchar_t* trait;
  };
  static const Sign signs[] = {
      {1, 20, L"Aquarius", L"Jan 20 – Feb 18", L"Air · Fixed", L"Ideas, distance, reform"},
      {2, 19, L"Pisces", L"Feb 19 – Mar 20", L"Water · Mutable", L"Dreams, mercy, tide"},
      {3, 21, L"Aries", L"Mar 21 – Apr 19", L"Fire · Cardinal", L"Beginnings, courage"},
      {4, 20, L"Taurus", L"Apr 20 – May 20", L"Earth · Fixed", L"Patience, craft, ground"},
      {5, 21, L"Gemini", L"May 21 – Jun 20", L"Air · Mutable", L"Speech, pairs, curiosity"},
      {6, 21, L"Cancer", L"Jun 21 – Jul 22", L"Water · Cardinal", L"Home, memory, care"},
      {7, 23, L"Leo", L"Jul 23 – Aug 22", L"Fire · Fixed", L"Warmth, stage, pride"},
      {8, 23, L"Virgo", L"Aug 23 – Sep 22", L"Earth · Mutable", L"Detail, harvest, repair"},
      {9, 23, L"Libra", L"Sep 23 – Oct 22", L"Air · Cardinal", L"Balance, pact, grace"},
      {10, 23, L"Scorpio", L"Oct 23 – Nov 21", L"Water · Fixed", L"Depth, reserve, insight"},
      {11, 22, L"Sagittarius", L"Nov 22 – Dec 21", L"Fire · Mutable", L"Road, aim, candor"},
      {12, 22, L"Capricorn", L"Dec 22 – Jan 19", L"Earth · Cardinal", L"Duty, peak, time"},
  };
  const Sign* chosen = &signs[11];
  for (const Sign& sign : signs) {
    if (month > sign.startMonth || (month == sign.startMonth && day >= sign.startDay)) chosen = &sign;
  }
  Almanac result;
  result.lunar = chosen->name;
  result.cycle = chosen->span;
  result.suitable = chosen->element;
  result.avoid = chosen->trait;
  return result;
}

Almanac almanacFor(int year, int month, int day) {
  if (calendarSettings().language == 1) return zodiacFor(month, day);
  Almanac result;
  const LunarDate lunar = lunarFromGregorian(year, month, day);
  result.lunar = lunarMonthDayText(lunar);
  const int yearNumber = pillarYear(year, month, day);
  const int yearStem = positiveMod(yearNumber - 4, 10);
  const int yearBranch = positiveMod(yearNumber - 4, 12);
  const int monthBranchIndex = monthBranch(year, month, day);
  const int yinStem = (yearStem % 5) * 2 + 2;
  const int monthStem = positiveMod(yinStem + positiveMod(monthBranchIndex - 2, 12), 10);
  const int dayIndex = positiveMod(julianDay(year, month, day) + 49, 60);
  const int dayStem = dayIndex % 10;
  const int dayBranch = dayIndex % 12;
  result.cycle = std::wstring(stems[yearStem]) + branches[yearBranch] + L"年 " + stems[monthStem] + branches[monthBranchIndex] + L"月 " + stems[dayStem] + branches[dayBranch] + L"日";
  const int duty = positiveMod(dayBranch - monthBranchIndex, 12);
  result.suitable = suitableByDuty[duty];
  result.avoid = avoidByDuty[duty];
  return result;
}
