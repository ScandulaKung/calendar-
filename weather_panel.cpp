#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <gdiplus.h>

#include "settings.hpp"
#include "theme.hpp"
#include "weather.hpp"
#include "weather_icons.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace Gdiplus;

namespace {

constexpr int kTimerRefresh = 1;

HWND panelWindow = nullptr;
RectF closeButton;

Color colorOf(BYTE red, BYTE green, BYTE blue) {
  return Color(255, red, green, blue);
}

void fillRoundRect(Graphics& graphics, const RectF& rect, float radius, Color color) {
  GraphicsPath path;
  const float diameter = std::min(radius * 2.f, std::min(rect.Width, rect.Height));
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

void paintPanel(Graphics& graphics, int width, int height) {
  const float scale = width / 700.f;
  graphics.SetSmoothingMode(SmoothingModeAntiAlias);
  graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
  drawThemedBackground(graphics, width, height);
  const Palette palette = currentPalette();
  const Color textColor = colorOf(palette.text[0], palette.text[1], palette.text[2]);
  const Color mutedColor = colorOf(palette.muted[0], palette.muted[1], palette.muted[2]);
  const Color cardColor = colorOf(palette.card[0], palette.card[1], palette.card[2]);
  FontFamily family(L"Microsoft YaHei");
  Font title(&family, 22.f * scale, FontStyleBold, UnitPixel);
  Font section(&family, 16.f * scale, FontStyleBold, UnitPixel);
  Font body(&family, 13.f * scale, FontStyleRegular, UnitPixel);
  Font small(&family, 12.f * scale, FontStyleRegular, UnitPixel);
  Font strong(&family, 15.f * scale, FontStyleBold, UnitPixel);
  Font huge(&family, 34.f * scale, FontStyleBold, UnitPixel);

  const bool english = englishUi();
  const WeatherSnapshot weather = currentWeather();
  const float margin = 22.f * scale;
  drawText(graphics, english ? L"Weather" : L"天气", title, RectF(margin, 14.f * scale, 120.f * scale, 32.f * scale), textColor, StringAlignmentNear);
  if (!weather.city.empty()) {
    drawText(graphics, weather.city.c_str(), body, RectF(margin + 64.f * scale, 16.f * scale, 180.f * scale, 28.f * scale), mutedColor, StringAlignmentNear);
  }
  wchar_t updated[32];
  if (english) std::swprintf(updated, 32, L"%02d:%02d updated", weather.updatedHour, weather.updatedMinute);
  else std::swprintf(updated, 32, L"%02d:%02d 更新", weather.updatedHour, weather.updatedMinute);
  closeButton = RectF(width - margin - 28.f * scale, 16.f * scale, 28.f * scale, 28.f * scale);
  if (weather.ready) {
    drawText(graphics, updated, small, RectF(width - 180.f * scale, 16.f * scale, 120.f * scale, 28.f * scale), mutedColor, StringAlignmentFar);
  }
  Pen closePen(mutedColor, 1.6f);
  const float cx = closeButton.X + closeButton.Width / 2.f;
  const float cy = closeButton.Y + closeButton.Height / 2.f;
  graphics.DrawLine(&closePen, cx - 6.f * scale, cy - 6.f * scale, cx + 6.f * scale, cy + 6.f * scale);
  graphics.DrawLine(&closePen, cx + 6.f * scale, cy - 6.f * scale, cx - 6.f * scale, cy + 6.f * scale);

  if (!weather.ready || weather.dayCount <= 0) {
    drawText(graphics, english ? L"Loading weather" : L"正在获取天气", section, RectF(0, height / 2.f - 20.f * scale, static_cast<float>(width), 40.f * scale), mutedColor, StringAlignmentCenter);
    return;
  }

  drawText(graphics, english ? L"Now" : L"实时天气", section, RectF(margin, 58.f * scale, 160.f * scale, 26.f * scale), textColor, StringAlignmentNear);
  const float icon = 62.f * scale;
  drawConditionIcon(graphics, margin, 96.f * scale, icon, weather.conditionCode, weather.code);
  wchar_t temperature[16];
  std::swprintf(temperature, 16, L"%.0f°C", weather.temperature);
  drawText(graphics, temperature, huge, RectF(margin + icon + 8.f * scale, 96.f * scale, 120.f * scale, 40.f * scale), textColor, StringAlignmentNear);
  const wchar_t* nowText = weather.conditionText.empty() ? weatherDescription(weather.code) : weather.conditionText.c_str();
  drawText(graphics, nowText, body, RectF(margin + icon + 8.f * scale, 136.f * scale, 140.f * scale, 24.f * scale), mutedColor, StringAlignmentNear);

  const wchar_t* cardLabelsZh[] = {L"空气质量", L"空气指数", L"风力", L"湿度"};
  const wchar_t* cardLabelsEn[] = {L"Air", L"AQI", L"Wind", L"Humidity"};
  const wchar_t* const* cardLabels = english ? cardLabelsEn : cardLabelsZh;
  wchar_t aqiNumber[16];
  std::swprintf(aqiNumber, 16, L"%d", weather.aqi);
  wchar_t humidity[16];
  if (weather.humidity >= 0) std::swprintf(humidity, 16, L"%d%%", weather.humidity);
  else std::swprintf(humidity, 16, L"—");
  const std::wstring wind = windDescription(weather.windSpeed, weather.windDirection);
  const wchar_t* cardValues[] = {airQualityLabel(weather.aqi), weather.aqi >= 0 ? aqiNumber : L"—", wind.c_str(), humidity};
  const float cardsX = 250.f * scale;
  const float cardGap = 10.f * scale;
  const float cardW = (width - cardsX - margin - cardGap * 3.f) / 4.f;
  const float cardH = 78.f * scale;
  for (int index = 0; index < 4; index += 1) {
    const RectF card(cardsX + index * (cardW + cardGap), 98.f * scale, cardW, cardH);
    fillRoundRect(graphics, card, 10.f * scale, cardColor);
    drawText(graphics, cardLabels[index], small, RectF(card.X, card.Y + 8.f * scale, card.Width, 22.f * scale), mutedColor, StringAlignmentCenter);
    Color valueColor = textColor;
    if (index == 0 && weather.aqi >= 0 && weather.aqi <= 50) valueColor = colorOf(46, 168, 84);
    drawText(graphics, cardValues[index], strong, RectF(card.X + 4.f * scale, card.Y + 34.f * scale, card.Width - 8.f * scale, 28.f * scale), valueColor, StringAlignmentCenter);
  }

  float cursor = 188.f * scale;
  if (!weather.warningText.empty()) {
    int red = 0;
    int green = 0;
    int blue = 0;
    warningRgb(weather, red, green, blue);
    std::wstring warning = (english ? L"Alert  " : L"预警  ") + weather.warningText;
    if (weather.warningCount > 1) warning += english ? L" +" + std::to_wstring(weather.warningCount - 1) : L" 等" + std::to_wstring(weather.warningCount) + L"条";
    drawText(graphics, warning.c_str(), body, RectF(margin, cursor, static_cast<float>(width) - margin * 2.f, 24.f * scale), colorOf(static_cast<BYTE>(red), static_cast<BYTE>(green), static_cast<BYTE>(blue)), StringAlignmentNear);
    cursor += 26.f * scale;
    if (!weather.warningNote.empty()) {
      drawText(graphics, weather.warningNote.c_str(), small, RectF(margin, cursor, static_cast<float>(width) - margin * 2.f, 20.f * scale), mutedColor, StringAlignmentNear);
      cursor += 22.f * scale;
    }
  }
  const bool astronomy = !weather.sunrise.empty() || !weather.sunset.empty() || !weather.moonrise.empty() || !weather.moonset.empty() || !weather.moonPhase.empty();
  if (astronomy) {
    drawText(graphics, english ? L"Sky" : L"天文", section, RectF(margin, cursor, 80.f * scale, 24.f * scale), textColor, StringAlignmentNear);
    cursor += 28.f * scale;
    const wchar_t* sunWord = english ? L"Sunrise " : L"日出 ";
    const wchar_t* setWord = english ? L"Sunset " : L"日落 ";
    const wchar_t* moonWord = english ? L"Moonrise " : L"月升 ";
    const wchar_t* moonSetWord = english ? L"Moonset " : L"月落 ";
    const wchar_t* phaseWord = english ? L"Phase " : L"月相 ";
    const std::wstring sunUp = std::wstring(sunWord) + (weather.sunrise.empty() ? std::wstring(L"—") : weather.sunrise);
    const std::wstring sunDown = std::wstring(setWord) + (weather.sunset.empty() ? std::wstring(L"—") : weather.sunset);
    const std::wstring moonUp = std::wstring(moonWord) + (weather.moonrise.empty() ? std::wstring(L"—") : weather.moonrise);
    const std::wstring moonDown = std::wstring(moonSetWord) + (weather.moonset.empty() ? std::wstring(L"—") : weather.moonset);
    const std::wstring phase = std::wstring(phaseWord) + (weather.moonPhase.empty() ? std::wstring(L"—") : weather.moonPhase);
    const wchar_t* fields[] = {sunUp.c_str(), sunDown.c_str(), moonUp.c_str(), moonDown.c_str(), phase.c_str()};
    const float fieldW = (static_cast<float>(width) - margin * 2.f) / 5.f;
    for (int index = 0; index < 5; index += 1) {
      drawText(graphics, fields[index], small, RectF(margin + index * fieldW, cursor, fieldW, 22.f * scale), textColor, StringAlignmentNear);
    }
    cursor += 30.f * scale;
  }
  const float forecastY = cursor + 4.f * scale;
  drawText(graphics, english ? L"Forecast" : L"天气预报", section, RectF(margin, forecastY, 160.f * scale, 26.f * scale), textColor, StringAlignmentNear);
  const int count = weather.dayCount;
  const float columnW = (width - margin * 2.f) / static_cast<float>(count);
  const float chartTop = forecastY + 234.f * scale;
  const float chartH = 120.f * scale;
  int lowBound = weather.days[0].low;
  int highBound = weather.days[0].high;
  for (int index = 0; index < count; index += 1) {
    lowBound = std::min(lowBound, weather.days[index].low);
    highBound = std::max(highBound, weather.days[index].high);
  }
  if (highBound <= lowBound) highBound = lowBound + 1;
  lowBound -= 2;
  highBound += 2;
  const float span = static_cast<float>(highBound - lowBound);

  SYSTEMTIME today{};
  GetLocalTime(&today);
  const wchar_t* weekNamesZh[] = {L"周日", L"周一", L"周二", L"周三", L"周四", L"周五", L"周六"};
  const wchar_t* weekNamesEn[] = {L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"};
  const wchar_t* const* weekNames = english ? weekNamesEn : weekNamesZh;
  PointF highPoints[7];
  PointF lowPoints[7];
  for (int index = 0; index < count; index += 1) {
    const ForecastDay& day = weather.days[index];
    const float left = margin + index * columnW;
    const bool isToday = day.year == today.wYear && day.month == today.wMonth && day.day == today.wDay;
    const wchar_t* week = isToday ? (english ? L"Today" : L"今天") : weekNames[std::max(0, std::min(6, day.weekday))];
    drawText(graphics, week, strong, RectF(left, forecastY + 36.f * scale, columnW, 24.f * scale), isToday ? textColor : mutedColor, StringAlignmentCenter);
    wchar_t date[16];
    if (english) std::swprintf(date, 16, L"%d/%d", day.month, day.day);
    else std::swprintf(date, 16, L"%d月%d日", day.month, day.day);
    drawText(graphics, date, small, RectF(left, forecastY + 60.f * scale, columnW, 20.f * scale), mutedColor, StringAlignmentCenter);
    drawConditionIcon(graphics, left + (columnW - 32.f * scale) / 2.f, forecastY + 88.f * scale, 32.f * scale, day.conditionCode, day.code);
    const wchar_t* dayLabel = day.conditionText.empty() ? weatherDescription(day.code) : day.conditionText.c_str();
    drawText(graphics, dayLabel, small, RectF(left, forecastY + 126.f * scale, columnW, 22.f * scale), mutedColor, StringAlignmentCenter);
    const float center = left + columnW / 2.f;
    highPoints[index] = PointF(center, chartTop + chartH - (day.high - lowBound) / span * chartH);
    lowPoints[index] = PointF(center, chartTop + chartH - (day.low - lowBound) / span * chartH);
  }

  if (count >= 2) {
    Pen highPen(colorOf(242, 162, 48), 2.f);
    Pen lowPen(colorOf(72, 156, 220), 2.f);
    graphics.DrawLines(&highPen, highPoints, count);
    graphics.DrawLines(&lowPen, lowPoints, count);
  }
  SolidBrush highDot(colorOf(242, 162, 48));
  SolidBrush lowDot(colorOf(72, 156, 220));
  for (int index = 0; index < count; index += 1) {
    graphics.FillEllipse(&highDot, highPoints[index].X - 3.5f * scale, highPoints[index].Y - 3.5f * scale, 7.f * scale, 7.f * scale);
    graphics.FillEllipse(&lowDot, lowPoints[index].X - 3.5f * scale, lowPoints[index].Y - 3.5f * scale, 7.f * scale, 7.f * scale);
    wchar_t highText[8];
    wchar_t lowText[8];
    std::swprintf(highText, 8, L"%d°", weather.days[index].high);
    std::swprintf(lowText, 8, L"%d°", weather.days[index].low);
    const float left = margin + index * columnW;
    drawText(graphics, highText, small, RectF(left, highPoints[index].Y - 20.f * scale, columnW, 18.f * scale), colorOf(214, 132, 32), StringAlignmentCenter);
    drawText(graphics, lowText, small, RectF(left, lowPoints[index].Y + 4.f * scale, columnW, 18.f * scale), colorOf(72, 140, 200), StringAlignmentCenter);
    const std::wstring dayWind = windDescription(weather.days[index].windSpeed, weather.days[index].windDirection);
    drawText(graphics, dayWind.c_str(), small, RectF(left, chartTop + chartH + 28.f * scale, columnW, 36.f * scale), mutedColor, StringAlignmentCenter);
  }
}

void placePanel(HWND owner) {
  RECT work{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  RECT main{};
  GetWindowRect(owner, &main);
  const FittedWindow fitted = fitDesign(700.f, 780.f);
  const int width = fitted.width;
  const int height = fitted.height;
  const int gap = 12;
  int x = main.right + gap;
  if (x + width > work.right) x = main.left - gap - width;
  if (x < work.left) x = work.left;
  if (x + width > work.right) x = work.right - width;
  int y = main.top;
  if (y + height > work.bottom) y = work.bottom - height;
  if (y < work.top) y = work.top;
  SetWindowPos(panelWindow, HWND_TOP, x, y, width, height, SWP_SHOWWINDOW);
}

LRESULT CALLBACK panelProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_CREATE:
      SetTimer(window, kTimerRefresh, 1000, nullptr);
      return 0;
    case WM_TIMER:
      InvalidateRect(window, nullptr, FALSE);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_KEYDOWN:
      if (wParam == VK_ESCAPE) ShowWindow(window, SW_HIDE);
      return 0;
    case WM_LBUTTONDOWN: {
      const int x = static_cast<short>(LOWORD(lParam));
      const int y = static_cast<short>(HIWORD(lParam));
      if (x >= closeButton.X && y >= closeButton.Y && x < closeButton.X + closeButton.Width && y < closeButton.Y + closeButton.Height) {
        ShowWindow(window, SW_HIDE);
      }
      return 0;
    }
    case WM_CLOSE:
      ShowWindow(window, SW_HIDE);
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT paint;
      HDC dc = BeginPaint(window, &paint);
      RECT client{};
      GetClientRect(window, &client);
      Bitmap buffer(client.right, client.bottom, PixelFormat32bppPARGB);
      Graphics graphics(&buffer);
      paintPanel(graphics, client.right, client.bottom);
      Graphics screen(dc);
      screen.DrawImage(&buffer, 0, 0);
      EndPaint(window, &paint);
      return 0;
    }
    case WM_DESTROY:
      KillTimer(window, kTimerRefresh);
      panelWindow = nullptr;
      return 0;
    default:
      return DefWindowProcW(window, message, wParam, lParam);
  }
}

}

void applyWeatherPanelSize() {
  if (!panelWindow) return;
  moveFittedWindow(panelWindow, fitDesign(700.f, 780.f));
}

void toggleWeatherPanel(HWND owner) {
  if (!owner) return;
  if (panelWindow && IsWindowVisible(panelWindow)) {
    ShowWindow(panelWindow, SW_HIDE);
    return;
  }
  if (!panelWindow) {
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = panelProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = L"YxCalendarWeather";
    RegisterClassW(&windowClass);
    panelWindow = CreateWindowExW(WS_EX_TOOLWINDOW, windowClass.lpszClassName, L"天气", WS_POPUP | WS_BORDER, 0, 0, 700, 640, owner, nullptr, windowClass.hInstance, nullptr);
    if (!panelWindow) return;
  }
  placePanel(owner);
  ShowWindow(panelWindow, SW_SHOW);
  SetForegroundWindow(panelWindow);
}

bool weatherPanelVisible() {
  return panelWindow && IsWindowVisible(panelWindow);
}

void invalidateWeatherPanel() {
  if (panelWindow) InvalidateRect(panelWindow, nullptr, FALSE);
}
