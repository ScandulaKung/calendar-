#include "china_cities.hpp"
#include "date_calc.hpp"
#include "lunar.hpp"

#include <cmath>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char* label) {
  if (condition) return;
  std::cerr << "failed: " << label << '\n';
  failures += 1;
}

}

int main() {
  expect(isLeapYear(2000), "2000 leap");
  expect(!isLeapYear(1900), "1900 common");
  expect(isLeapYear(2024), "2024 leap");
  expect(!isLeapYear(2025), "2025 common");
  expect(daysInMonth(2024, 2) == 29, "feb 2024");
  expect(daysInMonth(2025, 2) == 28, "feb 2025");
  expect(daysInMonth(2024, 4) == 30, "april");
  expect(daysInMonth(2024, 1) == 31, "january");

  expect(daySpan(2024, 1, 1, 2024, 1, 1) == 0, "same day");
  expect(daySpan(2024, 1, 1, 2025, 1, 1) == 366, "leap year span");
  expect(daySpan(2025, 1, 1, 2024, 1, 1) == -366, "reversed span");
  expect(daySpan(2024, 1, 1, 2024, 2, 1) == 31, "month span");
  expect(daySpan(2024, 2, 29, 2024, 3, 1) == 1, "leap day");
  expect(daySpan(2025, 2, 29, 2025, 3, 1) == -1, "invalid leap day");
  expect(daySpan(1899, 1, 1, 1900, 1, 1) == -1, "before 1900");
  expect(daySpan(2100, 12, 31, 2100, 12, 31) == 0, "year 2100");
  expect(daySpan(2101, 1, 1, 2101, 1, 1) == -1, "after 2100");

  expect(weekdayIndex(2024, 1, 1) == 1, "monday");
  expect(weekdayIndex(2024, 1, 6) == 6, "saturday");
  expect(weekdayIndex(2024, 1, 7) == 0, "sunday");
  expect(weekdayIndex(2024, 2, 4) == 0, "makeup sunday");

  const DateRangeResult newYear = calculateDateRange(2024, 1, 1, 2024, 1, 1);
  expect(newYear.ok && newYear.span == 0 && newYear.holidays == 1 && newYear.workdays == 0 && newYear.weekends == 0 && newYear.makeupDays == 0, "new year");

  const DateRangeResult makeup = calculateDateRange(2024, 2, 4, 2024, 2, 4);
  expect(makeup.weekends == 1 && makeup.makeupDays == 1 && makeup.workdays == 1 && makeup.holidays == 0, "makeup sunday counts");

  const DateRangeResult saturday = calculateDateRange(2024, 1, 6, 2024, 1, 6);
  expect(saturday.weekends == 1 && saturday.workdays == 0 && saturday.holidays == 0, "normal saturday");

  const DateRangeResult firstWeek = calculateDateRange(2024, 1, 1, 2024, 1, 7);
  expect(firstWeek.span == 6 && firstWeek.inclusiveDays == 7 && firstWeek.workdays == 4 && firstWeek.weekends == 2 && firstWeek.holidays == 1 && firstWeek.makeupDays == 0, "first week");

  const DateRangeResult midAutumn = calculateDateRange(2024, 9, 14, 2024, 9, 17);
  expect(midAutumn.span == 3 && midAutumn.weekends == 2 && midAutumn.holidays == 3 && midAutumn.makeupDays == 1 && midAutumn.workdays == 1, "mid autumn");

  const DateRangeResult reversed = calculateDateRange(2024, 9, 17, 2024, 9, 14);
  expect(reversed.span == midAutumn.span && reversed.workdays == midAutumn.workdays && reversed.earlier.day == 14, "reversed range");

  const DateRangeResult invalid = calculateDateRange(2025, 2, 29, 2025, 3, 1);
  expect(!invalid.ok, "invalid date");

  const DateRangeResult national2026 = calculateDateRange(2026, 10, 1, 2026, 10, 10);
  expect(national2026.holidays == 7 && national2026.makeupDays == 1, "2026 national day");
  const DateRangeResult newYearMakeup = calculateDateRange(2026, 1, 4, 2026, 1, 4);
  expect(newYearMakeup.makeupDays == 1 && newYearMakeup.workdays == 1 && newYearMakeup.weekends == 1, "2026 new year makeup");

  const LunarDate spring2023 = lunarFromGregorian(2023, 3, 21);
  expect(spring2023.month == 2 && spring2023.day == 30 && !spring2023.leap, "2023-03-21 lunar");
  const LunarDate may2023 = lunarFromGregorian(2023, 5, 30);
  expect(may2023.month == 4 && may2023.day == 12, "2023-05-30 lunar");
  const LunarDate newYear2024 = lunarFromGregorian(2024, 2, 10);
  expect(newYear2024.month == 1 && newYear2024.day == 1, "2024 spring festival");
  expect(lunarMonthDayText(may2023) == L"四月十二", "lunar text");
  expect(solarTermDay(2023, 4) == 6, "jingzhe 2023");
  expect(solarTermDay(2023, 5) == 21, "chunfen 2023");
  expect(daySubtitle(2023, 3, 8) == L"妇女节", "womens day");
  expect(daySubtitle(2023, 3, 12) == L"植树节", "arbor day");
  expect(daySubtitle(2023, 3, 21) == L"春分", "vernal equinox label");
  expect(daySubtitle(2023, 5, 19) == L"四月", "lunar first day shows the month");
  expect(daySubtitle(2024, 2, 10) == L"正月", "spring festival first day shows the month");
  expect(daySubtitle(2026, 9, 3) == L"抗战胜利", "victory memorial");
  expect(daySubtitle(2026, 12, 13) == L"国家公祭", "national memorial day");
  expect(daySubtitle(2026, 1, 10) == L"警察节", "police day");
  expect(daySubtitle(2026, 9, solarTermDay(2026, 17)) == L"丰收节", "harvest festival");

  const bool citiesLoaded = loadChinaCityList(L"..\\LocationList-master\\China-City-List-latest.csv")
    || loadChinaCityList(L"LocationList-master\\China-City-List-latest.csv")
    || loadChinaCityList(L"calendar\\LocationList-master\\China-City-List-latest.csv");
  expect(citiesLoaded, "china city list loads");
  expect(chineseCityName(22.3070, 114.1770, L"Hong Kong") == L"香港", "hong kong chinese name");
  expect(chineseCityName(22.3070, 114.1770, L"香港") == L"香港", "hong kong already chinese");
  expect(chineseCityName(39.9050, 116.4053, L"Beijing") == L"北京", "beijing chinese name");
  expect(chineseCityName(22.5470, 114.0859, L"Shenzhen") == L"深圳", "shenzhen chinese name");
  expect(chineseCityName(31.2994, 120.6196, L"Suzhou") == L"苏州", "suzhou jiangsu");
  expect(chineseCityName(33.6339, 116.9841, L"Suzhou") == L"宿州", "suzhou anhui");
  expect(chineseCityName(39.7439, 98.5112, L"Suzhou") == L"肃州", "suzhou gansu");
  expect(chineseCityName(34.3432, 108.9397, L"Xi'an") == L"西安", "xian chinese name");
  expect(chineseCityName(22.3090, 114.1710, L"") == L"油尖旺", "nearest district chinese name");
  expect(chineseCityName(30.70, 121.02, L"") == L"平湖", "pinghu nearest county");
  double jiaxingLatitude = 0;
  double jiaxingLongitude = 0;
  expect(chinaCityCenter(L"浙江省", L"嘉兴市", jiaxingLatitude, jiaxingLongitude), "jiaxing city center");
  expect(std::fabs(jiaxingLatitude - 30.7627) < 0.01 && std::fabs(jiaxingLongitude - 120.7509) < 0.01, "jiaxing center coordinates");
  expect(chineseCityName(35.6762, 139.6503, L"Tokyo").empty(), "city outside china stays unchanged");

  if (failures != 0) {
    std::cerr << failures << " failed\n";
    return 1;
  }
  std::cout << "calendar checks passed\n";
  return 0;
}
