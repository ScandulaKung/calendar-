#include "almanac.hpp"
#include "date_calc.hpp"
#include "extra.hpp"
#include "history.hpp"
#include "lunar.hpp"
#include "settings.hpp"
#include "voice.hpp"
#include "theme.hpp"
#include "version.hpp"
#include "weather.hpp"
#include "weather_icons.hpp"

#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace Gdiplus;

HWND windowHandle = nullptr;
constexpr UINT kShowExisting = WM_APP + 8;

namespace {

constexpr int kTimerClock = 1;
constexpr int kTimerIdle = 2;
constexpr int kTimerMotion = 3;

void syncMotionTimer(HWND target) {
  static bool running = false;
  const bool wanted = backgroundUsesMotion();
  if (wanted == running) return;
  if (wanted) SetTimer(target, kTimerMotion, 40, nullptr);
  else KillTimer(target, kTimerMotion);
  running = wanted;
}
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTrayId = 1;
bool trayIconAdded = false;
constexpr COLORREF kBackground = RGB(22, 30, 46);

HWND resultWindow = nullptr;
HINSTANCE instanceHandle = nullptr;
int screenDpi = 96;
int viewYear = 2026;
int viewMonth = 11;
int anchorYear = 0;
int anchorMonth = 0;
int anchorDay = 0;
int rangeYear = 0;
int rangeMonth = 0;
int rangeDay = 0;
bool hasAnchor = false;
bool hasRange = false;
int shownYear = 0;
int shownMonth = 0;
int shownDay = 0;
ULONG_PTR gdiToken = 0;

struct Cell {
  RectF bounds;
  int year = 0;
  int month = 0;
  int day = 0;
  bool inMonth = false;
};

Cell cells[42];
RectF todayButton;
RectF previousButton;
RectF nextButton;
RectF tabRects[5];
RectF headerMenuRect;
RectF weatherDetailButton;
POINT lastPointer{};
DWORD lastPointerTick = 0;

int dateKey(int year, int month, int day) {
  return year * 10000 + month * 100 + day;
}

bool sameDate(int year, int month, int day, int otherYear, int otherMonth, int otherDay) {
  return year == otherYear && month == otherMonth && day == otherDay;
}

void previousDay(int& year, int& month, int& day) {
  if (day > 1) {
    day -= 1;
    return;
  }
  if (month > 1) {
    month -= 1;
    day = daysInMonth(year, month);
    return;
  }
  year -= 1;
  month = 12;
  day = 31;
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

void shiftMonth(int delta) {
  viewMonth += delta;
  if (viewMonth < 1) {
    viewMonth = 12;
    viewYear -= 1;
  } else if (viewMonth > 12) {
    viewMonth = 1;
    viewYear += 1;
  }
  if (viewYear < 1900) {
    viewYear = 1900;
    viewMonth = 1;
  }
  if (viewYear > 2100) {
    viewYear = 2100;
    viewMonth = 12;
  }
}

DayTag tagFor(int year, int month, int day) {
  const DateRangeResult result = calculateDateRange(year, month, day, year, month, day);
  if (!result.ok) return DayTag::None;
  return result.earlier.tag;
}

Color colorOf(BYTE red, BYTE green, BYTE blue, BYTE alpha = 255) {
  return Color(alpha, red, green, blue);
}

void fillRoundRect(Graphics& graphics, const RectF& rect, float radius, Color color) {
  GraphicsPath path;
  const float diameter = radius * 2.f;
  path.AddArc(rect.X, rect.Y, diameter, diameter, 180.f, 90.f);
  path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter, 270.f, 90.f);
  path.AddArc(rect.X + rect.Width - diameter, rect.Y + rect.Height - diameter, diameter, diameter, 0.f, 90.f);
  path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter, 90.f, 90.f);
  path.CloseFigure();
  SolidBrush brush(color);
  graphics.FillPath(&brush, &path);
}

void drawText(Graphics& graphics, const wchar_t* text, const Font& font, const RectF& bounds, Color color, StringAlignment align) {
  SolidBrush brush(color);
  StringFormat format;
  format.SetAlignment(align);
  format.SetLineAlignment(StringAlignmentCenter);
  format.SetTrimming(StringTrimmingEllipsisCharacter);
  format.SetFormatFlags(StringFormatFlagsNoWrap);
  graphics.DrawString(text, -1, &font, bounds, &format, &brush);
}

void drawChevron(Graphics& graphics, const RectF& bounds, bool left, Color color) {
  Pen pen(color, 2.2f);
  pen.SetStartCap(LineCapRound);
  pen.SetEndCap(LineCapRound);
  const float cx = bounds.X + bounds.Width / 2.f;
  const float cy = bounds.Y + bounds.Height / 2.f;
  if (left) {
    graphics.DrawLine(&pen, cx + 4.f, cy - 6.f, cx - 4.f, cy);
    graphics.DrawLine(&pen, cx - 4.f, cy, cx + 4.f, cy + 6.f);
  } else {
    graphics.DrawLine(&pen, cx - 4.f, cy - 6.f, cx + 4.f, cy);
    graphics.DrawLine(&pen, cx + 4.f, cy, cx - 4.f, cy + 6.f);
  }
}

void drawWeather(Graphics& graphics, float width, float scale, const Font& valueFont, const Font& cityFont, Color textColor, Color mutedColor, Color cardColor, float leftLimit) {
  const WeatherSnapshot weather = currentWeather();
  const float edge = 10.f;
  const float diameter = 72.f * scale;
  const float gap = 8.f * scale;
  const float iconY = edge;
  if (!weather.ready) {
    drawConditionIcon(graphics, width - edge - diameter, iconY, diameter, -1, -1);
    return;
  }
  wchar_t temperature[32];
  std::swprintf(temperature, 32, L"%.0f°C", weather.temperature);
  const wchar_t* condition = weather.conditionText.empty() ? weatherDescription(weather.code) : weather.conditionText.c_str();
  RectF tempSize;
  RectF condSize;
  RectF citySize;
  graphics.MeasureString(temperature, -1, &valueFont, PointF(0.f, 0.f), &tempSize);
  graphics.MeasureString(condition, -1, &valueFont, PointF(0.f, 0.f), &condSize);
  if (!weather.city.empty()) graphics.MeasureString(weather.city.c_str(), -1, &cityFont, PointF(0.f, 0.f), &citySize);
  const float buttonW = 22.f * scale;
  const float buttonH = 18.f * scale;
  const float wordGap = 6.f * scale;
  const float cityRow = citySize.Width + (weather.city.empty() ? 0.f : 4.f * scale) + buttonW;
  float condW = condSize.Width;
  float topRow = condW + wordGap + tempSize.Width;
  float blockW = std::max(topRow, cityRow);
  const float textRight = width - edge;
  const float maxCluster = std::max(diameter + gap + std::max(tempSize.Width, cityRow), textRight - leftLimit);
  float cluster = diameter + gap + blockW;
  if (cluster > maxCluster) {
    condW = std::max(0.f, condW - (cluster - maxCluster));
    topRow = (condW > 1.f ? condW + wordGap : 0.f) + tempSize.Width;
    blockW = std::max(topRow, cityRow);
  }
  const float textX = textRight - blockW;
  const float iconX = textX - gap - diameter;
  const float lineY = iconY + 8.f * scale;
  const float lineH = 22.f * scale;
  const float tempX = textRight - tempSize.Width;
  drawConditionIcon(graphics, iconX, iconY, diameter, weather.conditionCode, weather.code);
  if (condW > 1.f) {
    drawText(graphics, condition, valueFont, RectF(tempX - wordGap - condW, lineY, condW, lineH), textColor, StringAlignmentFar);
  }
  drawText(graphics, temperature, valueFont, RectF(tempX, lineY, tempSize.Width, lineH), textColor, StringAlignmentFar);
  const float cityY = iconY + 34.f * scale;
  weatherDetailButton = RectF(textRight - buttonW, cityY, buttonW, buttonH);
  if (!weather.city.empty()) {
    drawText(graphics, weather.city.c_str(), cityFont, RectF(textX, cityY, blockW - buttonW - 4.f * scale, buttonH), mutedColor, StringAlignmentFar);
  }
  fillRoundRect(graphics, weatherDetailButton, 5.f * scale, cardColor);
  drawText(graphics, L"…", cityFont, weatherDetailButton, textColor, StringAlignmentCenter);
  if (!weather.warningText.empty()) {
    int red = 0;
    int green = 0;
    int blue = 0;
    warningRgb(weather, red, green, blue);
    std::wstring warning = (englishUi() ? L"Alert " : L"预警 ") + weather.warningText;
    if (weather.warningCount > 1) warning += englishUi() ? L" +" + std::to_wstring(weather.warningCount - 1) : L" 等" + std::to_wstring(weather.warningCount) + L"条";
    const float warnH = 16.f * scale;
    float warnY = iconY + diameter + 2.f;
    const float limit = 110.f * scale - warnH;
    if (warnY > limit) warnY = limit;
    const float warnW = std::min(diameter + gap + blockW, 200.f * scale);
    if (warnY > iconY + diameter * 0.45f) {
      drawText(graphics, warning.c_str(), cityFont, RectF(textRight - warnW, warnY, warnW, warnH), colorOf(static_cast<BYTE>(red), static_cast<BYTE>(green), static_cast<BYTE>(blue)), StringAlignmentFar);
    }
  }
}

void drawTabIcon(Graphics& graphics, int index, const RectF& bounds, Color color) {
  const float unit = bounds.Height / 16.f;
  Pen pen(color, std::max(1.6f, bounds.Height * 0.09f));
  pen.SetStartCap(LineCapRound);
  pen.SetEndCap(LineCapRound);
  pen.SetLineJoin(LineJoinRound);
  const float cx = bounds.X + bounds.Width / 2.f;
  const float top = bounds.Y;
  if (index == 0) {
    graphics.DrawRectangle(&pen, cx - 9.f * unit, top, 18.f * unit, 16.f * unit);
    graphics.DrawLine(&pen, cx - 5.f * unit, top + 5.f * unit, cx + 5.f * unit, top + 5.f * unit);
    graphics.DrawLine(&pen, cx - 5.f * unit, top + 10.f * unit, cx + 5.f * unit, top + 10.f * unit);
  } else if (index == 1) {
    graphics.DrawEllipse(&pen, cx - 7.f * unit, top + 1.f * unit, 14.f * unit, 14.f * unit);
    graphics.DrawLine(&pen, cx - 3.f * unit, top + 16.f * unit, cx + 8.f * unit, top + 5.f * unit);
  } else if (index == 3) {
    PointF diamond[] = {PointF(cx, top), PointF(cx + 9.f * unit, top + 8.f * unit), PointF(cx, top + 16.f * unit), PointF(cx - 9.f * unit, top + 8.f * unit)};
    graphics.DrawPolygon(&pen, diamond, 4);
  } else if (index == 4) {
    const float left = cx - 8.f * unit;
    const float right = cx + 8.f * unit;
    for (int row = 0; row < 3; row += 1) {
      const float lineY = top + 3.f * unit + row * 6.f * unit;
      graphics.DrawLine(&pen, left, lineY, right, lineY);
      const float knob = row == 1 ? cx - 2.f * unit : cx + 2.f * unit;
      const float knobSize = 4.8f * unit;
      graphics.DrawEllipse(&pen, knob - knobSize / 2.f, lineY - knobSize / 2.f, knobSize, knobSize);
    }
  }
}

void paintWindow(Graphics& graphics, int width, int height) {
  setBackgroundFrame(width, height);
  const float scale = height / 860.f;
  weatherDetailButton = RectF();
  graphics.SetSmoothingMode(SmoothingModeAntiAlias);
  graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
  drawThemedBackground(graphics, width, height);
  const Palette palette = currentPalette();
  const Color textColor = colorOf(palette.text[0], palette.text[1], palette.text[2]);
  const Color mutedColor = colorOf(palette.muted[0], palette.muted[1], palette.muted[2]);
  const Color cardColor = colorOf(palette.card[0], palette.card[1], palette.card[2]);
  const Color lineColor = colorOf(palette.line[0], palette.line[1], palette.line[2]);
  const Color weekendColor = colorOf(palette.weekend[0], palette.weekend[1], palette.weekend[2]);
  const Color dimColor = colorOf(palette.dim[0], palette.dim[1], palette.dim[2]);
  const bool dayTheme = calendarSettings().appearance == 1;
  const bool english = englishUi();

  const CalendarSettings& fontSettings = calendarSettings();
  const std::wstring faceName = fontSettings.clockFont.empty() ? L"Microsoft YaHei" : fontSettings.clockFont;
  FontFamily chosen(faceName.c_str());
  const std::wstring paintFace = chosen.IsAvailable() ? faceName : L"Microsoft YaHei";
  FontFamily family(L"Microsoft YaHei");
  const bool sizedFont = fontSettings.clockFontSize >= 8;
  const auto em = [&](float builtIn) {
    if (!sizedFont) return builtIn * scale;
    return fontSettings.clockFontSize * (builtIn / 20.f) * static_cast<float>(screenDpi) / 72.f * scale;
  };
  const auto boldOf = [&](bool builtInBold) { return sizedFont ? fontSettings.clockFontBold : builtInBold; };
  const bool italic = sizedFont && fontSettings.clockFontItalic;
  Font weatherFont(&family, 13.f * scale, FontStyleRegular, UnitPixel);
  Font subFont(&family, 11.f * scale, FontStyleRegular, UnitPixel);
  HDC fontDc = graphics.GetHDC();
  LOGFONTW lfClock = styledLogFont(paintFace, em(42.f), boldOf(true), italic);
  LOGFONTW lfDate = styledLogFont(paintFace, em(15.f), boldOf(false), italic);
  LOGFONTW lfMonth = styledLogFont(paintFace, em(18.f), boldOf(true), italic);
  LOGFONTW lfSmall = styledLogFont(paintFace, em(13.f), boldOf(false), italic);
  LOGFONTW lfDay = styledLogFont(paintFace, em(20.f), boldOf(true), italic);
  LOGFONTW lfClock12 = styledLogFont(paintFace, em(30.f), boldOf(true), italic);
  Font clockFont(fontDc, &lfClock);
  Font dateFont(fontDc, &lfDate);
  Font monthFont(fontDc, &lfMonth);
  Font smallFont(fontDc, &lfSmall);
  Font dayFont(fontDc, &lfDay);
  Font clock12(fontDc, &lfClock12);
  graphics.ReleaseHDC(fontDc);
  Font badgeFont(&family, 10.f * scale, FontStyleBold, UnitPixel);
  Font tabFont(&family, 12.f * scale, FontStyleRegular, UnitPixel);

  SYSTEMTIME now{};
  GetLocalTime(&now);
  wchar_t clock[32];
  const bool hour24 = calendarSettings().hour24;
  if (hour24) std::swprintf(clock, 32, L"%02d:%02d:%02d", now.wHour, now.wMinute, now.wSecond);
  else {
    int hour12 = now.wHour % 12;
    if (hour12 == 0) hour12 = 12;
    std::swprintf(clock, 32, L"%ls %d:%02d:%02d", now.wHour < 12 ? (english ? L"AM" : L"上午") : (english ? L"PM" : L"下午"), hour12, now.wMinute, now.wSecond);
  }
  drawText(graphics, clock, hour24 ? clockFont : clock12, RectF(22.f * scale, 18.f * scale, width - 210.f * scale, 52.f * scale), textColor, StringAlignmentNear);
  RectF clockSize;
  graphics.MeasureString(clock, -1, hour24 ? &clockFont : &clock12, PointF(0.f, 0.f), &clockSize);
  drawWeather(graphics, static_cast<float>(width), scale, weatherFont, weatherFont, textColor, mutedColor, cardColor, 22.f * scale + clockSize.Width + 8.f * scale);

  const int headerYear = hasAnchor ? anchorYear : now.wYear;
  const int headerMonth = hasAnchor ? anchorMonth : now.wMonth;
  const int headerDay = hasAnchor ? anchorDay : now.wDay;
  const LunarDate lunar = lunarFromGregorian(headerYear, headerMonth, headerDay);
  static const wchar_t* monthNames[] = {L"", L"January", L"February", L"March", L"April", L"May", L"June", L"July", L"August", L"September", L"October", L"November", L"December"};
  wchar_t header[64];
  std::wstring headerText;
  if (english) {
    std::swprintf(header, 64, L"%ls %d, %d", monthNames[headerMonth], headerDay, headerYear);
    headerText = header;
  } else {
    std::swprintf(header, 64, L"%d年%02d月%02d日 ", headerYear, headerMonth, headerDay);
    headerText = header + lunarMonthDayText(lunar);
  }
  drawText(graphics, headerText.c_str(), dateFont, RectF(22.f * scale, 72.f * scale, width - 210.f * scale, 28.f * scale), dayTheme ? colorOf(36, 102, 176) : colorOf(126, 176, 232), StringAlignmentNear);

  headerMenuRect = RectF(0.f, 0.f, static_cast<float>(width), 112.f * scale);
  Pen divider(lineColor, 1.f);
  graphics.DrawLine(&divider, 0.f, 112.f * scale, static_cast<float>(width), 112.f * scale);

  wchar_t monthLabel[32];
  if (english) std::swprintf(monthLabel, 32, L"%ls %d", monthNames[viewMonth], viewYear);
  else std::swprintf(monthLabel, 32, L"%d年%02d月", viewYear, viewMonth);
  drawText(graphics, monthLabel, monthFont, RectF(22.f * scale, 122.f * scale, 180.f * scale, 32.f * scale), textColor, StringAlignmentNear);

  const float buttonTop = 124.f * scale;
  const float buttonSize = 28.f * scale;
  nextButton = RectF(width - 22.f * scale - buttonSize, buttonTop, buttonSize, buttonSize);
  previousButton = RectF(nextButton.X - 8.f * scale - buttonSize, buttonTop, buttonSize, buttonSize);
  const float todayW = english ? 62.f * scale : 50.f * scale;
  todayButton = RectF(previousButton.X - (todayW + 8.f * scale), buttonTop, todayW, buttonSize);
  SolidBrush buttonBrush(cardColor);
  GraphicsPath todayPath;
  const float radius = 8.f * scale;
  todayPath.AddArc(todayButton.X, todayButton.Y, radius, radius, 180.f, 90.f);
  todayPath.AddArc(todayButton.X + todayButton.Width - radius, todayButton.Y, radius, radius, 270.f, 90.f);
  todayPath.AddArc(todayButton.X + todayButton.Width - radius, todayButton.Y + todayButton.Height - radius, radius, radius, 0.f, 90.f);
  todayPath.AddArc(todayButton.X, todayButton.Y + todayButton.Height - radius, radius, radius, 90.f, 90.f);
  todayPath.CloseFigure();
  graphics.FillPath(&buttonBrush, &todayPath);
  graphics.FillEllipse(&buttonBrush, previousButton);
  graphics.FillEllipse(&buttonBrush, nextButton);
  drawText(graphics, english ? L"Today" : L"今天", smallFont, todayButton, textColor, StringAlignmentCenter);
  drawChevron(graphics, previousButton, true, textColor);
  drawChevron(graphics, nextButton, false, textColor);

  const bool weekMonday = calendarSettings().weekStartsMonday;
  const wchar_t* weekMondayNames[] = {L"一", L"二", L"三", L"四", L"五", L"六", L"日"};
  const wchar_t* weekSundayNames[] = {L"日", L"一", L"二", L"三", L"四", L"五", L"六"};
  const wchar_t* weekMondayEnglish[] = {L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat", L"Sun"};
  const wchar_t* weekSundayEnglish[] = {L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"};
  const wchar_t* const* weekNames = english ? (weekMonday ? weekMondayEnglish : weekSundayEnglish) : (weekMonday ? weekMondayNames : weekSundayNames);
  const float gridLeft = 8.f * scale;
  const float gridWidth = width - gridLeft * 2.f;
  const float columnWidth = gridWidth / 7.f;
  const float tabsHeight = 78.f * scale;
  const float weekRow = 26.f * scale;
  const float availableTop = 158.f * scale;
  const float minInfo = 196.f * scale;
  float rowHeight = 62.f * scale;
  const float room = height - tabsHeight - availableTop - minInfo;
  if (weekRow + rowHeight * 6.f > room) rowHeight = std::max(46.f * scale, (room - weekRow) / 6.f);
  const float gridTop = availableTop;
  for (int column = 0; column < 7; column += 1) {
    const int columnWeekday = weekMonday ? (column + 1) % 7 : column;
    Color color = columnWeekday == 0 || columnWeekday == 6 ? weekendColor : mutedColor;
    drawText(graphics, weekNames[column], smallFont, RectF(gridLeft + column * columnWidth, gridTop, columnWidth, weekRow), color, StringAlignmentCenter);
  }

  int year = viewYear;
  int month = viewMonth;
  int day = 1;
  const int firstWeekday = weekdayIndex(viewYear, viewMonth, 1);
  const int leading = weekMonday ? (firstWeekday + 6) % 7 : firstWeekday;
  for (int count = 0; count < leading; count += 1) previousDay(year, month, day);

  const int anchorKey = hasAnchor ? dateKey(anchorYear, anchorMonth, anchorDay) : 0;
  const int rangeKey = hasRange ? dateKey(rangeYear, rangeMonth, rangeDay) : anchorKey;
  const int earlierKey = std::min(anchorKey, rangeKey);
  const int laterKey = std::max(anchorKey, rangeKey);

  for (int index = 0; index < 42; index += 1) {
    const int column = index % 7;
    const int row = index / 7;
    cells[index].bounds = RectF(gridLeft + column * columnWidth, gridTop + weekRow + row * rowHeight, columnWidth, rowHeight);
    cells[index].year = year;
    cells[index].month = month;
    cells[index].day = day;
    cells[index].inMonth = month == viewMonth && year == viewYear;
    const int key = dateKey(year, month, day);
    const bool inRange = hasRange && key >= earlierKey && key <= laterKey;
    const bool isEarlier = hasRange && key == earlierKey;
    const bool isLater = hasAnchor && key == laterKey;
    const RectF bounds = cells[index].bounds;
    const float circle = 36.f * scale;
    const float circleX = bounds.X + (bounds.Width - circle) / 2.f;
    const float circleY = bounds.Y + 4.f * scale;
    if (inRange && !isEarlier && !isLater) {
      SolidBrush band(cardColor);
      graphics.FillRectangle(&band, bounds.X + 2.f, circleY, bounds.Width - 4.f, circle);
    }
    if (isLater) {
      SolidBrush selected(colorOf(47, 120, 255));
      graphics.FillEllipse(&selected, circleX, circleY, circle, circle);
    } else if (isEarlier) {
      SolidBrush marked(dimColor);
      graphics.FillEllipse(&marked, circleX, circleY, circle, circle);
    }

    const DayTag tag = tagFor(year, month, day);
    const int weekday = weekdayIndex(year, month, day);
    const bool weekend = weekday == 0 || weekday == 6;
    Color numberColor = textColor;
    if (!cells[index].inMonth) numberColor = weekend ? weekendColor : dimColor;
    else if (weekend || tag == DayTag::Rest) numberColor = weekendColor;
    if (isLater || isEarlier) numberColor = colorOf(255, 255, 255);
    wchar_t number[4];
    std::swprintf(number, 4, L"%d", day);
    drawText(graphics, number, dayFont, RectF(bounds.X, circleY, bounds.Width, circle), numberColor, StringAlignmentCenter);

    if (tag != DayTag::None && cells[index].inMonth) {
      const float badgeW = 18.f * scale;
      const float badgeH = 16.f * scale;
      const RectF badge(circleX + circle - badgeW * 0.55f, circleY - badgeH * 0.42f, badgeW, badgeH);
      const bool rest = tag == DayTag::Rest;
      fillRoundRect(graphics, badge, 3.f * scale, rest ? colorOf(118, 196, 72) : colorOf(236, 78, 68));
      drawText(graphics, rest ? L"休" : L"班", badgeFont, badge, colorOf(255, 255, 255), StringAlignmentCenter);
    }

    const DayLabel label = english ? religiousLabel(year, month, day) : dayLabel(year, month, day);
    const std::wstring own = personalMark(month, day);
    Color subtitle = label.highlighted ? colorOf(255, 176, 96) : mutedColor;
    if (!cells[index].inMonth) subtitle = label.highlighted ? colorOf(196, 140, 84) : dimColor;
    if (isLater) subtitle = colorOf(232, 240, 255);
    const wchar_t* subtitleText = label.text.c_str();
    if (!own.empty() && !label.highlighted) {
      subtitleText = own.c_str();
      subtitle = colorOf(214, 92, 132);
    }
    drawText(graphics, subtitleText, subFont, RectF(bounds.X, circleY + circle + 1.f * scale, bounds.Width, 16.f * scale), subtitle, StringAlignmentCenter);
    if (!own.empty() && label.highlighted) {
      SolidBrush dot(colorOf(214, 92, 132));
      graphics.FillEllipse(&dot, bounds.X + bounds.Width / 2.f - 2.f * scale, circleY + circle + 15.f * scale, 4.f * scale, 4.f * scale);
    }

    nextDay(year, month, day);
  }

  const float infoTop = gridTop + weekRow + rowHeight * 6.f;
  const float infoBottom = height - tabsHeight;
  if (infoBottom > infoTop + 24.f) {
    const Almanac almanac = almanacFor(headerYear, headerMonth, headerDay);
    const HistorySnapshot history = currentHistory();
    Font infoTitle(&family, 13.f * scale, FontStyleBold, UnitPixel);
    Font infoBody(&family, 12.f * scale, FontStyleRegular, UnitPixel);
    Pen infoLine(lineColor, 1.f);
    graphics.DrawLine(&infoLine, 16.f * scale, infoTop + 4.f * scale, width - 16.f * scale, infoTop + 4.f * scale);
    const float left = 16.f * scale;
    const float textW = width - 32.f * scale;
    const float split = infoTop + (infoBottom - infoTop) * 0.52f;
    graphics.DrawLine(&infoLine, 16.f * scale, split, width - 16.f * scale, split);
    float textY = infoTop + 8.f * scale;
    drawText(graphics, english ? L"Horoscope" : L"黄历", infoTitle, RectF(left, textY, textW, 18.f * scale), textColor, StringAlignmentNear);
    textY += 18.f * scale;
    drawText(graphics, almanac.lunar.c_str(), infoBody, RectF(left, textY, textW, 18.f * scale), mutedColor, StringAlignmentNear);
    textY += 18.f * scale;
    drawText(graphics, almanac.cycle.c_str(), infoBody, RectF(left, textY, textW, 18.f * scale), mutedColor, StringAlignmentNear);
    textY += 18.f * scale;
    const std::wstring suitable = (english ? L"" : L"宜  ") + almanac.suitable;
    const std::wstring avoid = (english ? L"" : L"忌  ") + almanac.avoid;
    drawText(graphics, suitable.c_str(), infoBody, RectF(left, textY, textW, 18.f * scale), colorOf(92, 168, 96), StringAlignmentNear);
    textY += 18.f * scale;
    if (textY + 16.f * scale < split) drawText(graphics, avoid.c_str(), infoBody, RectF(left, textY, textW, 18.f * scale), weekendColor, StringAlignmentNear);
    float historyY = split + 6.f * scale;
    drawText(graphics, english ? L"On This Day" : L"历史上的今天", infoTitle, RectF(left, historyY, textW, 18.f * scale), textColor, StringAlignmentNear);
    historyY += 20.f * scale;
    if (!history.ready) {
      drawText(graphics, english ? L"Loading" : L"正在获取", infoBody, RectF(left, historyY, textW, 18.f * scale), mutedColor, StringAlignmentNear);
    }
    for (int index = 0; index < history.count && historyY + 18.f * scale < infoBottom; index += 1) {
      const std::wstring line = std::to_wstring(history.years[index]) + L"  " + history.texts[index];
      drawText(graphics, line.c_str(), infoBody, RectF(left, historyY, textW, 18.f * scale), mutedColor, StringAlignmentNear);
      historyY += 20.f * scale;
    }
  }

  const float cursor = height - tabsHeight;
  SolidBrush bar(colorOf(palette.bar[0], palette.bar[1], palette.bar[2]));
  graphics.FillRectangle(&bar, 0, static_cast<int>(cursor), width, static_cast<int>(tabsHeight + 2.f));
  const wchar_t* tabNamesZh[] = {L"计划", L"记录", L"", L"工具", L"设置"};
  const wchar_t* tabNamesEn[] = {L"Plans", L"Notes", L"", L"Tools", L"Settings"};
  const wchar_t* const* tabNames = english ? tabNamesEn : tabNamesZh;
  const float tabWidth = width / 5.f;
  for (int index = 0; index < 5; index += 1) {
    const RectF tab(index * tabWidth, cursor, tabWidth, tabsHeight);
    tabRects[index] = tab;
    const bool settingsTab = (index == 4 && settingsWindowVisible()) || (index == 0 && planWindowVisible()) || (index == 1 && notesWindowVisible()) || (index == 3 && toolsWindowVisible());
    const Color tabColor = settingsTab ? colorOf(47, 120, 255) : mutedColor;
    if (index == 2) {
      const float plus = 46.f * scale;
      SolidBrush plusBrush(colorOf(47, 120, 255));
      graphics.FillEllipse(&plusBrush, tab.X + (tab.Width - plus) / 2.f, cursor + 8.f * scale, plus, plus);
      Pen plusPen(colorOf(255, 255, 255), 2.4f);
      const float px = tab.X + tab.Width / 2.f;
      const float py = cursor + 8.f * scale + plus / 2.f;
      graphics.DrawLine(&plusPen, px - 9.f * scale, py, px + 9.f * scale, py);
      graphics.DrawLine(&plusPen, px, py - 9.f * scale, px, py + 9.f * scale);
    } else {
      const float iconBox = 27.2f * scale;
      const float labelGap = 8.f * scale;
      const float labelH = 16.f * scale;
      const float groupTop = tab.Y + (tabsHeight - iconBox - labelGap - labelH) / 2.0f;
      drawTabIcon(graphics, index, RectF(tab.X, groupTop, tab.Width, iconBox), tabColor);
      drawText(graphics, tabNames[index], tabFont, RectF(tab.X, groupTop + iconBox + labelGap, tab.Width, labelH), tabColor, StringAlignmentCenter);
    }
  }
  Pen edge(colorOf(58, 72, 96), 1.f);
  graphics.DrawRectangle(&edge, 0.f, 0.f, static_cast<float>(width - 1), static_cast<float>(height - 1));
}

void selectCell(const Cell& cell) {
  if (!isValidDate(cell.year, cell.month, cell.day)) return;
  if (!hasAnchor || hasRange || sameDate(cell.year, cell.month, cell.day, anchorYear, anchorMonth, anchorDay)) {
    hasAnchor = true;
    hasRange = false;
    anchorYear = cell.year;
    anchorMonth = cell.month;
    anchorDay = cell.day;
    return;
  }
  hasRange = true;
  rangeYear = cell.year;
  rangeMonth = cell.month;
  rangeDay = cell.day;
}

void goToToday() {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  viewYear = now.wYear;
  viewMonth = now.wMonth;
  hasAnchor = true;
  hasRange = false;
  anchorYear = now.wYear;
  anchorMonth = now.wMonth;
  anchorDay = now.wDay;
  shownYear = now.wYear;
  shownMonth = now.wMonth;
  shownDay = now.wDay;
}

bool followNewDay() {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  if (shownYear == 0) {
    shownYear = now.wYear;
    shownMonth = now.wMonth;
    shownDay = now.wDay;
    return false;
  }
  if (now.wYear == shownYear && now.wMonth == shownMonth && now.wDay == shownDay) return false;
  const bool trackingToday = !hasRange && hasAnchor && anchorYear == shownYear && anchorMonth == shownMonth && anchorDay == shownDay;
  const bool viewOnToday = viewYear == shownYear && viewMonth == shownMonth;
  shownYear = now.wYear;
  shownMonth = now.wMonth;
  shownDay = now.wDay;
  if (!trackingToday) return false;
  anchorYear = now.wYear;
  anchorMonth = now.wMonth;
  anchorDay = now.wDay;
  if (viewOnToday) {
    viewYear = now.wYear;
    viewMonth = now.wMonth;
  }
  return true;
}

DateRangeResult selectedRange() {
  const int firstKey = dateKey(anchorYear, anchorMonth, anchorDay);
  const int secondKey = dateKey(rangeYear, rangeMonth, rangeDay);
  const bool anchorFirst = firstKey <= secondKey;
  return calculateDateRange(
      anchorFirst ? anchorYear : rangeYear,
      anchorFirst ? anchorMonth : rangeMonth,
      anchorFirst ? anchorDay : rangeDay,
      anchorFirst ? rangeYear : anchorYear,
      anchorFirst ? rangeMonth : anchorMonth,
      anchorFirst ? rangeDay : anchorDay);
}

void placeResultWindow() {
  if (!resultWindow || !windowHandle) return;
  RECT work{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  RECT main{};
  GetWindowRect(windowHandle, &main);
  const FittedWindow fitted = fitDesign(308.f, 168.f);
  const int width = fitted.width;
  const int height = fitted.height;
  const int gap = 12;
  int x = main.right + gap;
  if (x + width > work.right) x = main.left - gap - width;
  if (x < work.left) x = work.left;
  if (x + width > work.right) x = std::max(static_cast<int>(work.left), static_cast<int>(work.right - width));

  int alignTop = main.top;
  int alignBottom = main.bottom;
  const int earlierKey = std::min(dateKey(anchorYear, anchorMonth, anchorDay), dateKey(rangeYear, rangeMonth, rangeDay));
  const int laterKey = std::max(dateKey(anchorYear, anchorMonth, anchorDay), dateKey(rangeYear, rangeMonth, rangeDay));
  bool found = false;
  int top = 0;
  int bottom = 0;
  for (const Cell& cell : cells) {
    const int key = dateKey(cell.year, cell.month, cell.day);
    if (key < earlierKey || key > laterKey) continue;
    POINT origin{static_cast<int>(cell.bounds.X), static_cast<int>(cell.bounds.Y)};
    ClientToScreen(windowHandle, &origin);
    const int cellBottom = origin.y + static_cast<int>(cell.bounds.Height);
    if (!found || origin.y < top) top = origin.y;
    if (!found || cellBottom > bottom) bottom = cellBottom;
    found = true;
  }
  if (found) {
    alignTop = top;
    alignBottom = bottom;
  }
  int y = (alignTop + alignBottom) / 2 - height / 2;
  if (y + height > work.bottom) y = work.bottom - height;
  if (y < work.top) y = work.top;

  RECT current{};
  if (IsWindowVisible(resultWindow)) GetWindowRect(resultWindow, &current);
  if (current.left == x && current.top == y && current.right - current.left == width && current.bottom - current.top == height) return;
  SetWindowPos(resultWindow, HWND_TOP, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void updateResultWindow() {
  if (!hasRange) {
    if (resultWindow) ShowWindow(resultWindow, SW_HIDE);
    return;
  }
  if (!resultWindow) {
    resultWindow = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        L"YxCalendarResult",
        L"日期计算",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0,
        0,
        308,
        168,
        windowHandle,
        nullptr,
        instanceHandle,
        nullptr);
    if (!resultWindow) return;
    BOOL dark = TRUE;
    DwmSetWindowAttribute(resultWindow, 20, &dark, sizeof(dark));
    SendMessageW(resultWindow, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(LoadIconW(instanceHandle, MAKEINTRESOURCEW(1))));
  }
  placeResultWindow();
  InvalidateRect(resultWindow, nullptr, FALSE);
}

LRESULT CALLBACK resultProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_CLOSE:
      hasRange = false;
      ShowWindow(window, SW_HIDE);
      if (windowHandle) InvalidateRect(windowHandle, nullptr, FALSE);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT paint;
      HDC dc = BeginPaint(window, &paint);
      RECT client{};
      GetClientRect(window, &client);
      Bitmap buffer(client.right, client.bottom, PixelFormat32bppPARGB);
      Graphics graphics(&buffer);
      graphics.SetSmoothingMode(SmoothingModeAntiAlias);
      graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
      drawThemedBackground(graphics, client.right, client.bottom);
      const Palette palette = currentPalette();
      const Color textColor = colorOf(palette.text[0], palette.text[1], palette.text[2]);
      const Color mutedColor = colorOf(palette.muted[0], palette.muted[1], palette.muted[2]);
      FontFamily family(L"Microsoft YaHei");
      const float scale = client.bottom / 150.f;
      Font title(&family, 15.f * scale, FontStyleBold, UnitPixel);
      Font body(&family, 13.f * scale, FontStyleRegular, UnitPixel);
      if (!hasRange) {
        drawText(graphics, englishUi() ? L"Pick a start date, then an end date" : L"先点开始日期，再点结束日期", body, RectF(12, 12, client.right - 24.f, client.bottom - 24.f), mutedColor, StringAlignmentCenter);
      } else {
        const DateRangeResult result = selectedRange();
        if (!result.ok) {
          drawText(graphics, englishUi() ? L"Workday count is only for 2024-2025" : L"超出工作日计算范围(2024-2025年)", body, RectF(16, 16, client.right - 32.f, client.bottom - 32.f), colorOf(255, 176, 96), StringAlignmentCenter);
        } else {
          wchar_t line[96];
          if (englishUi()) std::swprintf(line, 96, L"%d-%02d-%02d  —  %d-%02d-%02d", result.earlier.year, result.earlier.month, result.earlier.day, result.later.year, result.later.month, result.later.day);
          else std::swprintf(line, 96, L"%d年%d月%d日  —  %d年%d月%d日", result.earlier.year, result.earlier.month, result.earlier.day, result.later.year, result.later.month, result.later.day);
          drawText(graphics, line, title, RectF(16, 12, client.right - 32.f, 28.f * scale), textColor, StringAlignmentNear);
          if (englishUi()) {
            std::swprintf(line, 96, L"Span  %d", result.span);
            drawText(graphics, line, body, RectF(16, 44.f * scale, client.right - 32.f, 22.f * scale), mutedColor, StringAlignmentNear);
            std::swprintf(line, 96, L"Days inside  %d", result.inclusiveDays);
            drawText(graphics, line, body, RectF(16, 68.f * scale, client.right - 32.f, 22.f * scale), mutedColor, StringAlignmentNear);
            std::swprintf(line, 96, L"Workdays %d    Weekends %d", result.workdays, result.weekends);
            drawText(graphics, line, body, RectF(16, 92.f * scale, client.right - 32.f, 22.f * scale), mutedColor, StringAlignmentNear);
            std::swprintf(line, 96, L"Holidays %d    Makeup %d", result.holidays, result.makeupDays);
          } else {
          std::swprintf(line, 96, L"间隔天数  %d", result.span);
          drawText(graphics, line, body, RectF(16, 44.f * scale, client.right - 32.f, 22.f * scale), mutedColor, StringAlignmentNear);
          std::swprintf(line, 96, L"范围内  %d 天", result.inclusiveDays);
          drawText(graphics, line, body, RectF(16, 68.f * scale, client.right - 32.f, 22.f * scale), mutedColor, StringAlignmentNear);
          std::swprintf(line, 96, L"工作日 %d    周末 %d", result.workdays, result.weekends);
          drawText(graphics, line, body, RectF(16, 92.f * scale, client.right - 32.f, 22.f * scale), mutedColor, StringAlignmentNear);
          std::swprintf(line, 96, L"法定假日 %d    调休上班 %d", result.holidays, result.makeupDays);
          }
          drawText(graphics, line, body, RectF(16, 116.f * scale, client.right - 32.f, 22.f * scale), mutedColor, StringAlignmentNear);
        }
      }
      Graphics screen(dc);
      screen.DrawImage(&buffer, 0, 0);
      EndPaint(window, &paint);
      return 0;
    }
    default:
      return DefWindowProcW(window, message, wParam, lParam);
  }
}

bool contains(const RectF& bounds, int x, int y) {
  return x >= bounds.X && y >= bounds.Y && x < bounds.X + bounds.Width && y < bounds.Y + bounds.Height;
}

NOTIFYICONDATAW trayIconData() {
  NOTIFYICONDATAW data{};
  data.cbSize = sizeof(data);
  data.hWnd = windowHandle;
  data.uID = kTrayId;
  data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  data.uCallbackMessage = kTrayMessage;
  data.hIcon = static_cast<HICON>(LoadImageW(instanceHandle, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
  lstrcpynW(data.szTip, L"日历", static_cast<int>(sizeof(data.szTip) / sizeof(data.szTip[0])));
  return data;
}

void addTrayIcon() {
  if (trayIconAdded) return;
  NOTIFYICONDATAW data = trayIconData();
  trayIconAdded = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
}

void removeTrayIcon() {
  if (!trayIconAdded) return;
  NOTIFYICONDATAW data = trayIconData();
  Shell_NotifyIconW(NIM_DELETE, &data);
  trayIconAdded = false;
}

void setTaskbarButton(HWND window, bool show) {
  LONG extra = GetWindowLongW(window, GWL_EXSTYLE);
  if (show) extra = (extra & ~WS_EX_TOOLWINDOW) | WS_EX_APPWINDOW;
  else extra = (extra & ~WS_EX_APPWINDOW) | WS_EX_TOOLWINDOW;
  SetWindowLongW(window, GWL_EXSTYLE, extra);
  SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void restoreFromTray(HWND window) {
  removeTrayIcon();
  setTaskbarButton(window, true);
  ShowWindow(window, SW_SHOW);
  SetForegroundWindow(window);
  if (hasRange) updateResultWindow();
}

void minimizeToTray(HWND window) {
  addTrayIcon();
  if (weatherPanelVisible()) toggleWeatherPanel(window);
  if (resultWindow) ShowWindow(resultWindow, SW_HIDE);
  ShowWindow(window, SW_HIDE);
}

void concealFromDock(HWND window) {
  removeTrayIcon();
  if (weatherPanelVisible()) toggleWeatherPanel(window);
  if (resultWindow) ShowWindow(resultWindow, SW_HIDE);
  ShowWindow(window, SW_HIDE);
  setTaskbarButton(window, false);
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case kShowExisting:
      restoreFromTray(window);
      return 0;
    case WM_CREATE:
      goToToday();
      startHistoryUpdates();
      promptQWeatherCredentials(window, false);
      startWeatherUpdates();
      lastPointerTick = GetTickCount();
      GetCursorPos(&lastPointer);
      SetTimer(window, kTimerClock, 1000, nullptr);
      SetTimer(window, kTimerIdle, 500, nullptr);
      return 0;
    case WM_TIMER:
      if (wParam == kTimerMotion) {
        if (advanceBackground()) {
          InvalidateRect(window, nullptr, FALSE);
          if (weatherPanelVisible()) invalidateWeatherPanel();
          if (resultWindow && IsWindowVisible(resultWindow)) InvalidateRect(resultWindow, nullptr, FALSE);
        }
        return 0;
      }
      if (wParam == kTimerIdle) {
        POINT cursor{};
        GetCursorPos(&cursor);
        if (cursor.x != lastPointer.x || cursor.y != lastPointer.y) {
          lastPointer = cursor;
          lastPointerTick = GetTickCount();
        } else if (hasRange && GetTickCount() - lastPointerTick >= 5000) {
          goToToday();
          updateResultWindow();
          InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
      }
      if (followNewDay()) {
        requestWeatherRefresh();
        if (weatherPanelVisible()) invalidateWeatherPanel();
      }
      pollVoiceClock();
      if (pollBackgroundShuffle()) {
        InvalidateRect(window, nullptr, FALSE);
        if (weatherPanelVisible()) invalidateWeatherPanel();
        if (resultWindow && IsWindowVisible(resultWindow)) InvalidateRect(resultWindow, nullptr, FALSE);
      }
      updateDockClock();
      InvalidateRect(window, nullptr, FALSE);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT paint;
      HDC dc = BeginPaint(window, &paint);
      RECT client{};
      GetClientRect(window, &client);
      Bitmap buffer(client.right, client.bottom, PixelFormat32bppPARGB);
      Graphics graphics(&buffer);
      paintWindow(graphics, client.right, client.bottom);
      if (hasRange) placeResultWindow();
      Graphics screen(dc);
      screen.DrawImage(&buffer, 0, 0);
      EndPaint(window, &paint);
      syncMotionTimer(window);
      return 0;
    }
    case WM_LBUTTONDOWN: {
      lastPointerTick = GetTickCount();
      const int x = static_cast<short>(LOWORD(lParam));
      const int y = static_cast<short>(HIWORD(lParam));
      if (contains(weatherDetailButton, x, y)) {
        toggleWeatherPanel(window);
        return 0;
      }
      if (contains(headerMenuRect, x, y)) {
        ReleaseCapture();
        SendMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        if (hasRange) placeResultWindow();
        return 0;
      }
      for (int index = 0; index < 5; index += 1) {
        if (!contains(tabRects[index], x, y)) continue;
        if (index == 0) showPlanWindow(window);
        else if (index == 1) showNotesWindow(window);
        else if (index == 3) showToolsWindow(window);
        else if (index == 4) showSettingsWindow(window);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
      }
      if (contains(todayButton, x, y)) goToToday();
      else if (contains(previousButton, x, y)) shiftMonth(-1);
      else if (contains(nextButton, x, y)) shiftMonth(1);
      else {
        for (const Cell& cell : cells) {
          if (contains(cell.bounds, x, y)) {
            selectCell(cell);
            break;
          }
        }
      }
      updateResultWindow();
      InvalidateRect(window, nullptr, FALSE);
      return 0;
    }
    case WM_MOVE:
      if (hasRange && !IsIconic(window)) placeResultWindow();
      return 0;
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED: {
      HDC screen = GetDC(nullptr);
      screenDpi = GetDeviceCaps(screen, LOGPIXELSX);
      ReleaseDC(nullptr, screen);
      if (screenDpi <= 0) screenDpi = 96;
      moveFittedWindow(window, fitDesign(430.f, 900.f));
      applySettingsWindowSize();
      applyToolsNotesSize();
      applyPlanWindowSize();
      applyWeatherPanelSize();
      if (hasRange) placeResultWindow();
      InvalidateRect(window, nullptr, FALSE);
      return 0;
    }
    case WM_RBUTTONUP: {
      const int x = static_cast<short>(LOWORD(lParam));
      const int y = static_cast<short>(HIWORD(lParam));
      if (!contains(headerMenuRect, x, y)) return 0;
      HMENU menu = CreatePopupMenu();
      AppendMenuW(menu, MF_STRING, 1, L"退出程序");
      POINT point{};
      GetCursorPos(&point);
      const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window, nullptr);
      DestroyMenu(menu);
      if (command == 1) DestroyWindow(window);
      return 0;
    }
    case WM_NCCALCSIZE:
      if (wParam) {
        // The proposed rectangle is the whole window. Pull the top up by one
        // pixel so Windows 10's light frame line sits outside the visible window.
        NCCALCSIZE_PARAMS* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
        params->rgrc[0].top -= 1;
        return 0;
      }
      return DefWindowProcW(window, message, wParam, lParam);
    case WM_NCHITTEST: {
      const LRESULT hit = DefWindowProcW(window, message, wParam, lParam);
      switch (hit) {
        case HTLEFT:
        case HTRIGHT:
        case HTTOP:
        case HTBOTTOM:
        case HTTOPLEFT:
        case HTTOPRIGHT:
        case HTBOTTOMLEFT:
        case HTBOTTOMRIGHT:
          return HTCLIENT;
        default:
          return hit;
      }
    }
    case WM_SYSCOMMAND:
      if ((wParam & 0xFFF0) == SC_MAXIMIZE) return 0;
      if ((wParam & 0xFFF0) == SC_MINIMIZE) {
        minimizeToTray(window);
        return 0;
      }
      return DefWindowProcW(window, message, wParam, lParam);
    case kTrayMessage:
      if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
        restoreFromTray(window);
      } else if (lParam == WM_RBUTTONUP) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 1, englishUi() ? L"Show calendar" : L"显示日历");
        AppendMenuW(menu, MF_STRING, 2, englishUi() ? L"Quit" : L"退出程序");
        POINT point{};
        GetCursorPos(&point);
        SetForegroundWindow(window);
        const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window, nullptr);
        DestroyMenu(menu);
        PostMessageW(window, WM_NULL, 0, 0);
        if (command == 1) restoreFromTray(window);
        if (command == 2) DestroyWindow(window);
      }
      return 0;
    case WM_DESTROY:
      removeTrayIcon();
      stopHistoryUpdates();
      stopWeatherUpdates();
      KillTimer(window, kTimerClock);
      KillTimer(window, kTimerIdle);
      KillTimer(window, kTimerMotion);
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(window, message, wParam, lParam);
  }
}

}

void invalidateCalendar() {
  if (windowHandle) InvalidateRect(windowHandle, nullptr, FALSE);
}

void restoreCalendar() {
  if (windowHandle) restoreFromTray(windowHandle);
}

void toggleCalendarFromDock() {
  if (!windowHandle) return;
  if (IsWindowVisible(windowHandle)) concealFromDock(windowHandle);
  else restoreFromTray(windowHandle);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
  const wchar_t mutexName[] = L"Local\\YxCalendar." CALENDAR_VERSION_WSTRING;
  const wchar_t monthClass[] = L"YxCalendarMonth." CALENDAR_VERSION_WSTRING;
  SetLastError(0);
  HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, mutexName);
  const DWORD mutexError = GetLastError();
  if (instanceMutex && mutexError == ERROR_ALREADY_EXISTS) {
    HWND existing = nullptr;
    for (int attempt = 0; attempt < 30 && !existing; attempt += 1) {
      existing = FindWindowW(monthClass, nullptr);
      if (!existing) Sleep(100);
    }
    if (existing) {
      DWORD pid = 0;
      GetWindowThreadProcessId(existing, &pid);
      if (pid != 0) AllowSetForegroundWindow(pid);
      PostMessageW(existing, kShowExisting, 0, 0);
    }
    CloseHandle(instanceMutex);
    return 0;
  }

  instanceHandle = instance;
  GdiplusStartupInput input;
  if (GdiplusStartup(&gdiToken, &input, nullptr) != Ok) return 1;

  WNDCLASSW windowClass{};
  windowClass.lpfnWndProc = windowProc;
  windowClass.hInstance = instance;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
  windowClass.hbrBackground = CreateSolidBrush(kBackground);
  windowClass.lpszClassName = monthClass;
  RegisterClassW(&windowClass);

  WNDCLASSW resultClass{};
  resultClass.lpfnWndProc = resultProc;
  resultClass.hInstance = instance;
  resultClass.hCursor = windowClass.hCursor;
  resultClass.hIcon = windowClass.hIcon;
  resultClass.hbrBackground = windowClass.hbrBackground;
  resultClass.lpszClassName = L"YxCalendarResult";
  RegisterClassW(&resultClass);

  HDC screen = GetDC(nullptr);
  screenDpi = GetDeviceCaps(screen, LOGPIXELSX);
  ReleaseDC(nullptr, screen);
  if (screenDpi <= 0) screenDpi = 96;
  const FittedWindow fitted = fitDesign(430.f, 900.f);
  const int width = fitted.width;
  const int height = fitted.height;
  RECT work{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  int windowX = work.left + std::max(0, static_cast<int>(work.right - work.left - width) / 2);
  int windowY = work.top + std::max(0, static_cast<int>(work.bottom - work.top - height) / 2);
  windowHandle = CreateWindowExW(
      WS_EX_APPWINDOW,
      windowClass.lpszClassName,
      L"日历",
      WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
      windowX,
      windowY,
      width,
      height,
      nullptr,
      nullptr,
      instance,
      nullptr);
  SendMessageW(windowHandle, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(windowClass.hIcon));
  SendMessageW(windowHandle, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(windowClass.hIcon));
  BOOL dark = TRUE;
  DwmSetWindowAttribute(windowHandle, 20, &dark, sizeof(dark));
  MARGINS frame{1, 1, 1, 1};
  DwmExtendFrameIntoClientArea(windowHandle, &frame);
  SetWindowPos(windowHandle, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  ShowWindow(windowHandle, showCommand);
  UpdateWindow(windowHandle);
  bindCalendarWindow(windowHandle);
  applyWindowOpacity();
  updateDockClock();

  MSG message;
  while (GetMessageW(&message, nullptr, 0, 0)) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  GdiplusShutdown(gdiToken);
  if (instanceMutex) CloseHandle(instanceMutex);
  return static_cast<int>(message.wParam);
}
