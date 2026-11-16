#include "extra.hpp"

#include "date_calc.hpp"
#include "log.hpp"
#include "history.hpp"
#include "lunar.hpp"
#include "paths.hpp"
#include "settings.hpp"
#include "weather.hpp"

#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <gdiplus.h>

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

HWND calendarWindow = nullptr;
HWND toolsWindow = nullptr;
HWND notesWindow = nullptr;
HWND alertWindow = nullptr;
HWND dockWindow = nullptr;
float uiScale = 1.f;
int toolPage = 0;
int toolYear = 2026;
int toolMonth = 1;
int toolDay = 1;
int festivalBuiltYear = 0;
int festivalBuiltLang = -1;
int festivalScroll = 0;
int historyScroll = 0;
struct FestivalRow {
  int month = 1;
  int day = 1;
  std::wstring name;
};
std::vector<FestivalRow> festivalRows;
int noteMonth = 1;
int noteDay = 1;
int noteSelected = -1;
HWND noteEdit = nullptr;
std::wstring alertTitle;
std::wstring alertDetail;
bool alertGif = false;
std::wstring alertGifLoaded;
Gdiplus::Image* alertGifImage = nullptr;
std::vector<int> alertGifDelays;
UINT alertGifFrames = 1;
UINT alertGifFrame = 0;
ULONGLONG alertGifNext = 0;

struct Mark {
  int kind = 0;
  int month = 1;
  int day = 1;
  std::wstring name;
};

std::vector<Mark> marks;
bool marksLoaded = false;

struct Hit {
  int id = 0;
  Gdiplus::RectF rect;
};

std::vector<Hit> toolHits;
std::vector<Hit> noteHits;

float dp(float value) { return value * uiScale; }

std::wstring utf8ToWide(const std::string& text) {
  if (text.empty()) return L"";
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (size <= 0) return L"";
  std::wstring wide(size, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
  return wide;
}

std::string wideToUtf8(const std::wstring& text) {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string utf8(size, '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr);
  return utf8;
}

const wchar_t* tr(const wchar_t* zh, const wchar_t* en) { return englishUi() ? en : zh; }

using Gdiplus::Color;
using Gdiplus::Font;
using Gdiplus::FontStyleBold;
using Gdiplus::FontStyleItalic;
using Gdiplus::FontStyleRegular;
using Gdiplus::Graphics;
using Gdiplus::Pen;
using Gdiplus::RectF;
using Gdiplus::SolidBrush;
using Gdiplus::StringAlignmentCenter;
using Gdiplus::StringAlignmentNear;
using Gdiplus::StringFormat;
using Gdiplus::UnitPixel;

Color colorOf(BYTE red, BYTE green, BYTE blue) { return Color(255, red, green, blue); }

void drawText(Graphics& graphics, const wchar_t* text, const Font& font, const RectF& bounds, Color color, Gdiplus::StringAlignment align, bool wrap = false, bool ellipsis = true) {
  SolidBrush brush(color);
  StringFormat format;
  format.SetAlignment(align);
  format.SetLineAlignment(wrap ? Gdiplus::StringAlignmentNear : StringAlignmentCenter);
  if (!wrap) format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
  format.SetTrimming(ellipsis ? Gdiplus::StringTrimmingEllipsisCharacter : Gdiplus::StringTrimmingNone);
  graphics.DrawString(text, -1, &font, bounds, &format, &brush);
}

void addHit(std::vector<Hit>& hits, int id, const RectF& rect) { hits.push_back(Hit{id, rect}); }

int hitTest(const std::vector<Hit>& hits, int x, int y) {
  for (int index = static_cast<int>(hits.size()) - 1; index >= 0; index -= 1) {
    const RectF& rect = hits[index].rect;
    if (x >= rect.X && y >= rect.Y && x < rect.X + rect.Width && y < rect.Y + rect.Height) return hits[index].id;
  }
  return 0;
}

void readScale() {
  uiScale = fitDesign(kPanelWidth, kPanelHeight).scale;
}

void placeNear(HWND owner, int width, int height, int& x, int& y) {
  RECT work{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  RECT parent{};
  if (owner) GetWindowRect(owner, &parent);
  x = static_cast<int>(parent.right) + 12;
  y = static_cast<int>(parent.top) + 36;
  if (x + width > work.right) x = std::max(static_cast<int>(work.left), static_cast<int>(parent.left) - width - 12);
  if (x < work.left) x = static_cast<int>(work.left);
  if (y + height > work.bottom) y = std::max(static_cast<int>(work.top), static_cast<int>(work.bottom) - height - 8);
}

std::wstring datesPath() { return dataPath(L"personal_dates.txt"); }

void loadMarks() {
  if (marksLoaded) return;
  marksLoaded = true;
  marks.clear();
  std::ifstream input{std::filesystem::path(datesPath())};
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    int kind = 0;
    int month = 0;
    int day = 0;
    const int read = std::sscanf(line.c_str(), "%d,%d,%d,", &kind, &month, &day);
    const std::string::size_type third = line.find(',');
    const std::string::size_type fourth = third == std::string::npos ? std::string::npos : line.find(',', third + 1);
    const std::string::size_type fifth = fourth == std::string::npos ? std::string::npos : line.find(',', fourth + 1);
    if (read != 3 || fifth == std::string::npos || month < 1 || month > 12 || day < 1 || day > 31) continue;
    Mark mark;
    mark.kind = kind < 0 || kind > 2 ? 2 : kind;
    mark.month = month;
    mark.day = day;
    mark.name = utf8ToWide(line.substr(fifth + 1));
    if (!mark.name.empty()) marks.push_back(mark);
  }
}

void saveMarks() {
  std::ofstream output{std::filesystem::path(datesPath()), std::ios::binary};
  if (!output) return;
  for (const Mark& mark : marks) {
    std::wstring name = mark.name;
    for (wchar_t& character : name) {
      if (character == L',' || character == L'\n' || character == L'\r') character = L' ';
    }
    output << mark.kind << ',' << mark.month << ',' << mark.day << ',' << wideToUtf8(name) << '\n';
  }
}

const wchar_t* kindName(int kind) {
  if (kind == 0) return tr(L"生日", L"Birthday");
  if (kind == 1) return tr(L"结婚纪念日", L"Anniversary");
  return tr(L"纪念日", L"Date");
}

std::wstring markLabel(const Mark& mark) {
  if (mark.kind == 0 || mark.kind == 1) {
    const std::wstring prefix = kindName(mark.kind);
    if (mark.name.find(prefix) == std::wstring::npos) return prefix + L" " + mark.name;
  }
  return mark.name;
}

const wchar_t* presetTitle(int index) {
  static const wchar_t* zh[] = {L"生日", L"结婚纪念日", L"恋爱纪念日", L"入职纪念日"};
  static const wchar_t* en[] = {L"Birthday", L"Wedding", L"Together", L"Work day"};
  if (index < 0 || index > 3) return L"";
  return tr(zh[index], en[index]);
}

void rebuildFestivals() {
  const int lang = englishUi() ? 1 : 0;
  if (festivalBuiltYear == toolYear && festivalBuiltLang == lang) return;
  festivalBuiltYear = toolYear;
  festivalBuiltLang = lang;
  festivalRows.clear();
  for (int month = 1; month <= 12; month += 1) {
    const int limit = daysInMonth(toolYear, month);
    for (int day = 1; day <= limit; day += 1) {
      const std::wstring name = listedFestival(toolYear, month, day, lang == 1);
      if (!name.empty()) festivalRows.push_back(FestivalRow{month, day, name});
    }
  }
}

void stepDate(int& month, int& day, int delta, bool monthStep) {
  if (monthStep) {
    month += delta;
    if (month < 1) month = 12;
    if (month > 12) month = 1;
  } else day += delta;
  const int limit = daysInMonth(2024, month);
  if (day < 1) day = limit;
  if (day > limit) day = 1;
}

void drawBrand(Graphics& graphics) {
  const float x = dp(14);
  const float y = dp(14);
  const int pixels = std::max(16, static_cast<int>(dp(32)));
  static HICON programIcon = nullptr;
  if (!programIcon) programIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, pixels, pixels, LR_DEFAULTCOLOR));
  if (programIcon) {
    HDC hdc = graphics.GetHDC();
    DrawIconEx(hdc, static_cast<int>(x), static_cast<int>(y), programIcon, pixels, pixels, 0, nullptr, DI_NORMAL);
    graphics.ReleaseHDC(hdc);
  }
  Font font(L"Microsoft YaHei", dp(16), FontStyleBold, UnitPixel);
  drawText(graphics, tr(L"蕊历", L"Calendar"), font, RectF(x + static_cast<float>(pixels) + dp(6), y, dp(100), static_cast<float>(pixels)), colorOf(45, 45, 45), StringAlignmentNear);
}

void drawChrome(Graphics& graphics, int width, int height, const wchar_t* title, std::vector<Hit>& hits, const wchar_t* const* pages, int pageCount, int page) {
  hits.clear();
  SolidBrush content(colorOf(236, 236, 236));
  SolidBrush sidebar(colorOf(188, 188, 188));
  graphics.FillRectangle(&content, 0, 0, width, height);
  graphics.FillRectangle(&sidebar, 0.f, 0.f, dp(168), static_cast<float>(height));
  drawBrand(graphics);
  Font titleFont(L"Microsoft YaHei", dp(16), FontStyleBold, UnitPixel);
  Font nav(L"Microsoft YaHei", dp(14), FontStyleRegular, UnitPixel);
  Font navBold(L"Microsoft YaHei", dp(14), FontStyleBold, UnitPixel);
  drawText(graphics, title, titleFont, RectF(dp(188), dp(16), dp(280), dp(28)), colorOf(40, 40, 40), StringAlignmentNear);
  for (int index = 0; index < pageCount; index += 1) {
    const float y = dp(72) + index * dp(48);
    if (page == index) {
      SolidBrush bar(colorOf(226, 68, 68));
      graphics.FillRectangle(&bar, 0, static_cast<int>(y), static_cast<int>(dp(4)), static_cast<int>(dp(36)));
    }
    drawText(graphics, pages[index], page == index ? navBold : nav, RectF(dp(22), y, dp(130), dp(36)), colorOf(55, 55, 55), StringAlignmentNear);
    addHit(hits, 10 + index, RectF(0, y, dp(168), dp(42)));
  }
  Pen xpen(colorOf(80, 80, 80), 1.6f);
  const float x0 = static_cast<float>(width) - dp(28);
  const float y0 = dp(16);
  graphics.DrawLine(&xpen, x0, y0, x0 + dp(12), y0 + dp(12));
  graphics.DrawLine(&xpen, x0 + dp(12), y0, x0, y0 + dp(12));
  addHit(hits, 1, RectF(x0 - dp(8), y0 - dp(8), dp(28), dp(28)));
}

void drawStepper(Graphics& graphics, const Font& font, std::vector<Hit>& hits, float x, float y, const std::wstring& text, int minusId, int plusId) {
  RectF minus(x, y, dp(28), dp(28));
  RectF value(x + dp(32), y, dp(88), dp(28));
  RectF plus(x + dp(124), y, dp(28), dp(28));
  SolidBrush white(colorOf(255, 255, 255));
  Pen border(colorOf(214, 214, 214), 1.f);
  graphics.FillRectangle(&white, minus);
  graphics.FillRectangle(&white, value);
  graphics.FillRectangle(&white, plus);
  graphics.DrawRectangle(&border, minus);
  graphics.DrawRectangle(&border, value);
  graphics.DrawRectangle(&border, plus);
  drawText(graphics, L"-", font, minus, colorOf(60, 60, 60), StringAlignmentCenter);
  drawText(graphics, text.c_str(), font, value, colorOf(40, 40, 40), StringAlignmentCenter);
  drawText(graphics, L"+", font, plus, colorOf(60, 60, 60), StringAlignmentCenter);
  addHit(hits, minusId, minus);
  addHit(hits, plusId, plus);
}

void paintTools(Graphics& graphics, int width, int height) {
  const wchar_t* pages[] = {tr(L"节日", L"Festivals"), tr(L"历史上的今天", L"On This Day"), tr(L"世界时钟", L"World clock")};
  drawChrome(graphics, width, height, tr(L"工具", L"Tools"), toolHits, pages, 3, toolPage);
  Font body(L"Microsoft YaHei", dp(14), FontStyleRegular, UnitPixel);
  Font small(L"Microsoft YaHei", dp(13), FontStyleRegular, UnitPixel);
  const float x = dp(188);
  wchar_t number[32];
  if (toolPage == 2) {
    struct Zone {
      const wchar_t* zh;
      const wchar_t* en;
      int minutes;
    };
    const Zone zones[] = {
        {L"北京", L"Beijing", 8 * 60},
        {L"东京", L"Tokyo", 9 * 60},
        {L"悉尼", L"Sydney", 10 * 60},
        {L"新德里", L"New Delhi", 5 * 60 + 30},
        {L"迪拜", L"Dubai", 4 * 60},
        {L"莫斯科", L"Moscow", 3 * 60},
        {L"巴黎", L"Paris", 60},
        {L"伦敦", L"London", 0},
        {L"纽约", L"New York", -5 * 60},
        {L"洛杉矶", L"Los Angeles", -8 * 60},
    };
    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    FILETIME file{};
    SystemTimeToFileTime(&utc, &file);
    ULARGE_INTEGER ticks{};
    ticks.LowPart = file.dwLowDateTime;
    ticks.HighPart = file.dwHighDateTime;
    float y = dp(64);
    for (const Zone& zone : zones) {
      ULARGE_INTEGER shifted = ticks;
      const long long delta = static_cast<long long>(zone.minutes) * 60LL * 10000000LL;
      if (delta < 0) shifted.QuadPart -= static_cast<unsigned long long>(-delta);
      else shifted.QuadPart += static_cast<unsigned long long>(delta);
      FILETIME shiftedFile{shifted.LowPart, shifted.HighPart};
      SYSTEMTIME local{};
      FileTimeToSystemTime(&shiftedFile, &local);
      std::swprintf(number, 32, L"%02d:%02d", local.wHour, local.wMinute);
      wchar_t offset[16];
      const int hours = zone.minutes / 60;
      const int mins = std::abs(zone.minutes % 60);
      if (mins) std::swprintf(offset, 16, L"UTC%+d:%02d", hours, mins);
      else std::swprintf(offset, 16, L"UTC%+d", hours);
      drawText(graphics, tr(zone.zh, zone.en), body, RectF(x, y, dp(120), dp(28)), colorOf(40, 40, 40), StringAlignmentNear);
      drawText(graphics, number, body, RectF(x + dp(130), y, dp(80), dp(28)), colorOf(214, 52, 52), StringAlignmentNear);
      drawText(graphics, offset, small, RectF(x + dp(220), y, dp(110), dp(28)), colorOf(110, 110, 110), StringAlignmentNear);
      y += dp(36);
    }
    drawText(graphics, tr(L"按标准时计算，不含夏令时。", L"Standard time, without daylight saving."), small, RectF(x, y + dp(8), dp(360), dp(24)), colorOf(120, 120, 120), StringAlignmentNear);
    return;
  }
  if (toolPage == 0) {
    rebuildFestivals();
    drawText(graphics, tr(L"年", L"Year"), small, RectF(x, dp(64), dp(40), dp(28)), colorOf(90, 90, 90), StringAlignmentNear);
    std::swprintf(number, 32, L"%d", toolYear);
    drawStepper(graphics, body, toolHits, x + dp(36), dp(64), number, 25, 26);
    drawText(graphics, tr(L"只列出这一年的节日。", L"Festivals in this year."), small, RectF(x + dp(200), dp(64), dp(240), dp(28)), colorOf(120, 120, 120), StringAlignmentNear);
    const float top = dp(112);
    const float rowH = dp(28);
    const float view = static_cast<float>(height) - top - dp(12);
    const int maxScroll = std::max(0, static_cast<int>(festivalRows.size() * rowH - view));
    if (festivalScroll < 0) festivalScroll = 0;
    if (festivalScroll > maxScroll) festivalScroll = maxScroll;
    graphics.SetClip(RectF(x, top, dp(460), view));
    float y = top - static_cast<float>(festivalScroll);
    for (const FestivalRow& row : festivalRows) {
      if (y + rowH >= top && y <= top + view) {
        wchar_t text[80];
        std::swprintf(text, 80, L"%02d-%02d   %ls", row.month, row.day, row.name.c_str());
        drawText(graphics, text, body, RectF(x, y, dp(440), rowH), colorOf(50, 50, 50), StringAlignmentNear);
      }
      y += rowH;
    }
    graphics.ResetClip();
    return;
  }
  drawText(graphics, tr(L"月", L"Month"), small, RectF(x, dp(64), dp(40), dp(28)), colorOf(90, 90, 90), StringAlignmentNear);
  std::swprintf(number, 32, L"%d", toolMonth);
  drawStepper(graphics, body, toolHits, x + dp(36), dp(64), number, 21, 22);
  drawText(graphics, tr(L"日", L"Day"), small, RectF(x + dp(210), dp(64), dp(40), dp(28)), colorOf(90, 90, 90), StringAlignmentNear);
  std::swprintf(number, 32, L"%d", toolDay);
  drawStepper(graphics, body, toolHits, x + dp(246), dp(64), number, 23, 24);
  {
    const std::vector<HistoryEntry> lines = historyForDate(toolMonth, toolDay);
    const float top = dp(112);
    const float viewH = std::max(dp(40), static_cast<float>(height) - top - dp(12));
    if (lines.empty()) {
      drawText(graphics, tr(L"这一天的记录还没准备好。", L"No saved events for this day yet."), body, RectF(x, top, dp(420), dp(28)), colorOf(120, 120, 120), StringAlignmentNear);
      return;
    }
    struct HistoryRow { std::wstring text; float height; };
    std::vector<HistoryRow> rows;
    float total = 0.f;
    Gdiplus::StringFormat measureFormat;
    measureFormat.SetTrimming(Gdiplus::StringTrimmingNone);
    for (const HistoryEntry& entry : lines) {
      HistoryRow row;
      row.text = std::to_wstring(entry.year) + L"  " + entry.text;
      Gdiplus::RectF layout(0.f, 0.f, dp(440), 4000.f);
      Gdiplus::RectF bounds;
      graphics.MeasureString(row.text.c_str(), -1, &small, layout, &measureFormat, &bounds);
      row.height = std::max(dp(28), bounds.Height + dp(8));
      total += row.height;
      rows.push_back(std::move(row));
    }
    const int maxScroll = std::max(0, static_cast<int>(total - viewH));
    if (historyScroll < 0) historyScroll = 0;
    if (historyScroll > maxScroll) historyScroll = maxScroll;
    graphics.SetClip(RectF(x, top, dp(460), viewH));
    float y = top - static_cast<float>(historyScroll);
    for (const HistoryRow& row : rows) {
      if (y + row.height >= top && y <= top + viewH) {
        drawText(graphics, row.text.c_str(), small, RectF(x, y, dp(440), row.height), colorOf(55, 55, 55), StringAlignmentNear, true, false);
      }
      y += row.height;
    }
    graphics.ResetClip();
  }
}

void paintNotes(Graphics& graphics, int width, int height) {
  const wchar_t* pages[] = {tr(L"个人日期", L"Personal dates")};
  noteHits.clear();
  drawChrome(graphics, width, height, tr(L"记录", L"Notes"), noteHits, pages, 1, 0);
  Font body(L"Microsoft YaHei", dp(14), FontStyleRegular, UnitPixel);
  Font small(L"Microsoft YaHei", dp(13), FontStyleRegular, UnitPixel);
  const float x = dp(188);
  drawText(graphics, tr(L"纪念日名称", L"Name"), small, RectF(x, dp(56), dp(160), dp(22)), colorOf(90, 90, 90), StringAlignmentNear);
  const float widths[] = {dp(72), dp(112), dp(112), dp(112)};
  float chipX = x;
  float chipY = dp(82);
  for (int index = 0; index < 4; index += 1) {
    if (chipX + widths[index] > x + dp(420)) {
      chipX = x;
      chipY += dp(36);
    }
    RectF chip(chipX, chipY, widths[index], dp(28));
    SolidBrush whiteChip(colorOf(255, 255, 255));
    Pen chipBorder(colorOf(214, 214, 214), 1.f);
    graphics.FillRectangle(&whiteChip, chip);
    graphics.DrawRectangle(&chipBorder, chip);
    drawText(graphics, presetTitle(index), small, chip, colorOf(60, 60, 60), StringAlignmentCenter);
    addHit(noteHits, 40 + index, chip);
    chipX += widths[index] + dp(8);
  }
  const float formY = chipY + dp(40);
  wchar_t number[16];
  std::swprintf(number, 16, L"%d", noteMonth);
  drawStepper(graphics, body, noteHits, x, formY, number, 33, 34);
  std::swprintf(number, 16, L"%d", noteDay);
  drawStepper(graphics, body, noteHits, x + dp(180), formY, number, 35, 36);
  RectF nameBox(x, formY + dp(42), dp(220), dp(30));
  SolidBrush white(colorOf(255, 255, 255));
  Pen border(colorOf(214, 214, 214), 1.f);
  graphics.FillRectangle(&white, nameBox);
  graphics.DrawRectangle(&border, nameBox);
  RectF add(x + dp(232), formY + dp(42), dp(72), dp(30));
  SolidBrush red(colorOf(214, 52, 52));
  graphics.FillRectangle(&red, add);
  drawText(graphics, tr(L"添加", L"Add"), small, add, colorOf(255, 255, 255), StringAlignmentCenter);
  addHit(noteHits, 37, add);
  RectF remove(x + dp(312), formY + dp(42), dp(72), dp(30));
  graphics.FillRectangle(&white, remove);
  graphics.DrawRectangle(&border, remove);
  drawText(graphics, tr(L"删除", L"Delete"), small, remove, colorOf(60, 60, 60), StringAlignmentCenter);
  addHit(noteHits, 38, remove);
  float y = formY + dp(84);
  if (marks.empty()) drawText(graphics, tr(L"还没有个人日期。", L"No personal dates yet."), small, RectF(x, y, dp(320), dp(24)), colorOf(120, 120, 120), StringAlignmentNear);
  for (int index = 0; index < static_cast<int>(marks.size()) && y < static_cast<float>(height) - dp(16); index += 1) {
    RectF row(x, y, dp(400), dp(30));
    if (index == noteSelected) {
      SolidBrush mark(colorOf(232, 240, 250));
      graphics.FillRectangle(&mark, row);
    }
    wchar_t text[160];
    const std::wstring label = markLabel(marks[index]);
    std::swprintf(text, 160, L"%d/%d  %ls", marks[index].month, marks[index].day, label.c_str());
    drawText(graphics, text, small, RectF(row.X + dp(8), row.Y, row.Width - dp(12), row.Height), colorOf(50, 50, 50), StringAlignmentNear);
    addHit(noteHits, 100 + index, row);
    y += dp(32);
  }
  if (noteEdit) {
    MoveWindow(noteEdit, static_cast<int>(nameBox.X + dp(4)), static_cast<int>(nameBox.Y + dp(4)), static_cast<int>(nameBox.Width - dp(8)), static_cast<int>(nameBox.Height - dp(8)), TRUE);
    if (!IsWindowVisible(noteEdit)) ShowWindow(noteEdit, SW_SHOW);
  }
}

std::wstring alertGifFile(const wchar_t* name) {
  if (!name || !name[0]) return {};
  const std::wstring local = dataPath(name);
  if (GetFileAttributesW(local.c_str()) != INVALID_FILE_ATTRIBUTES) return local;
  return std::wstring(L"D:\\MyProjects\\calendar\\") + name;
}

void releaseAlertGif() {
  delete alertGifImage;
  alertGifImage = nullptr;
  alertGifLoaded.clear();
  alertGifDelays.clear();
  alertGifFrames = 1;
  alertGifFrame = 0;
}

void ensureAlertGif(const wchar_t* name) {
  const std::wstring path = alertGifFile(name);
  if (path.empty()) {
    releaseAlertGif();
    return;
  }
  if (alertGifImage && alertGifLoaded == path) return;
  releaseAlertGif();
  auto* image = new Gdiplus::Image(path.c_str());
  if (image->GetLastStatus() != Gdiplus::Ok) {
    logMissingResource(path);
    delete image;
    return;
  }
  UINT frames = image->GetFrameCount(&Gdiplus::FrameDimensionTime);
  if (frames < 1) frames = 1;
  alertGifFrames = frames;
  alertGifDelays.assign(frames, 100);
  const UINT size = image->GetPropertyItemSize(PropertyTagFrameDelay);
  if (size > 0) {
    std::vector<unsigned char> bytes(size);
    auto* item = reinterpret_cast<Gdiplus::PropertyItem*>(bytes.data());
    if (image->GetPropertyItem(PropertyTagFrameDelay, size, item) == Gdiplus::Ok && item->value) {
      const UINT count = std::min(frames, static_cast<UINT>(item->length / sizeof(long)));
      const auto* values = static_cast<const long*>(item->value);
      for (UINT index = 0; index < count; index += 1) {
        const int hundredths = values[index] <= 1 ? 10 : static_cast<int>(values[index]);
        alertGifDelays[index] = std::max(20, hundredths * 10);
      }
    }
  }
  alertGifFrame = 0;
  image->SelectActiveFrame(&Gdiplus::FrameDimensionTime, 0);
  alertGifNext = GetTickCount64() + static_cast<ULONGLONG>(alertGifDelays.empty() ? 100 : alertGifDelays[0]);
  alertGifLoaded = path;
  alertGifImage = image;
}

bool advanceAlertGif() {
  if (!alertGifImage || alertGifFrames < 2) return false;
  const ULONGLONG now = GetTickCount64();
  if (now < alertGifNext) return false;
  alertGifFrame = (alertGifFrame + 1) % alertGifFrames;
  alertGifImage->SelectActiveFrame(&Gdiplus::FrameDimensionTime, alertGifFrame);
  const int delay = alertGifDelays.empty() ? 100 : alertGifDelays[alertGifFrame % alertGifDelays.size()];
  alertGifNext = now + static_cast<ULONGLONG>(std::max(20, delay));
  return true;
}

void drawGifCover(Graphics& graphics, Gdiplus::Image* image, const RectF& box) {
  if (!image || image->GetWidth() == 0 || image->GetHeight() == 0) return;
  const float sourceWidth = static_cast<float>(image->GetWidth());
  const float sourceHeight = static_cast<float>(image->GetHeight());
  const float scale = std::max(box.Width / sourceWidth, box.Height / sourceHeight);
  const float drawnWidth = sourceWidth * scale;
  const float drawnHeight = sourceHeight * scale;
  graphics.SetClip(box);
  graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
  graphics.DrawImage(image, box.X + (box.Width - drawnWidth) / 2.f, box.Y + (box.Height - drawnHeight) / 2.f, drawnWidth, drawnHeight);
  graphics.ResetClip();
}

void paintAlert(Graphics& graphics, int width, int height) {
  SolidBrush fill(colorOf(255, 255, 255));
  graphics.FillRectangle(&fill, 0, 0, width, height);
  Font title(L"Microsoft YaHei", dp(16), FontStyleBold, UnitPixel);
  Font body(L"Microsoft YaHei", dp(14), FontStyleRegular, UnitPixel);
  const wchar_t* hint = calendarIsConcealed() ? tr(L"点击打开日历", L"Click to open") : tr(L"点击关闭", L"Click to close");
  if (alertGif && alertGifImage) {
    const float pad = dp(16);
    const float gap = dp(18);
    const float inset = dp(4);
    const float box = static_cast<float>(height) - inset * 2.f;
    const float boxX = static_cast<float>(width) - inset - box;
    drawGifCover(graphics, alertGifImage, RectF(boxX, inset, box, box));
    const float textW = boxX - gap - pad;
    drawText(graphics, alertTitle.c_str(), title, RectF(pad, dp(18), textW, dp(28)), colorOf(40, 40, 40), StringAlignmentNear);
    drawText(graphics, alertDetail.c_str(), body, RectF(pad, dp(50), textW, dp(28)), colorOf(90, 90, 90), StringAlignmentNear);
    drawText(graphics, hint, body, RectF(pad, dp(82), textW, dp(24)), colorOf(214, 52, 52), StringAlignmentNear);
  } else {
    drawText(graphics, alertTitle.c_str(), title, RectF(dp(16), dp(18), static_cast<float>(width) - dp(32), dp(28)), colorOf(40, 40, 40), StringAlignmentNear);
    drawText(graphics, alertDetail.c_str(), body, RectF(dp(16), dp(50), static_cast<float>(width) - dp(32), dp(28)), colorOf(90, 90, 90), StringAlignmentNear);
    drawText(graphics, hint, body, RectF(dp(16), dp(82), static_cast<float>(width) - dp(32), dp(24)), colorOf(214, 52, 52), StringAlignmentNear);
  }
  SolidBrush bar(colorOf(214, 52, 52));
  graphics.FillRectangle(&bar, 0, 0, width, static_cast<int>(dp(8)));
  Pen edge(colorOf(214, 214, 214), 1.f);
  graphics.DrawRectangle(&edge, 0.f, 0.f, static_cast<float>(width - 1), static_cast<float>(height - 1));
}

void paintDock(Graphics& graphics, int width, int height) {
  SolidBrush fill(colorOf(32, 36, 44));
  graphics.FillRectangle(&fill, 0, 0, width, height);
  const CalendarSettings& fontSettings = calendarSettings();
  const std::wstring& chosenFace = fontSettings.dockFont.empty() ? fontSettings.clockFont : fontSettings.dockFont;
  const std::wstring faceName = chosenFace.empty() ? L"Microsoft YaHei" : chosenFace;
  Gdiplus::FontFamily chosen(faceName.c_str());
  const std::wstring paintFace = chosen.IsAvailable() ? faceName : L"Microsoft YaHei";
  HDC screenDc = GetDC(nullptr);
  const float dpi = static_cast<float>(std::max(96, GetDeviceCaps(screenDc, LOGPIXELSY)));
  ReleaseDC(nullptr, screenDc);
  const bool sized = fontSettings.dockFontSize >= 8;
  const float em = sized ? fontSettings.dockFontSize * dpi / 72.f : std::max(12.f, height * 0.28f);
  LOGFONTW lf = styledLogFont(paintFace, em, sized && fontSettings.dockFontBold, sized && fontSettings.dockFontItalic);
  HDC fontDc = graphics.GetHDC();
  Font font(fontDc, &lf);
  graphics.ReleaseHDC(fontDc);
  const Gdiplus::StringAlignment align = calendarSettings().clockAlign == 0 ? Gdiplus::StringAlignmentNear : calendarSettings().clockAlign == 2 ? Gdiplus::StringAlignmentFar : StringAlignmentCenter;
  const std::wstring text = activeClockText();
  drawText(graphics, text.c_str(), font, RectF(6.f, 2.f, static_cast<float>(width) - 12.f, static_cast<float>(height) - 4.f), colorOf(245, 245, 245), align, true);
}

LRESULT CALLBACK toolsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_CREATE:
      SetTimer(window, 1, 1000, nullptr);
      return 0;
    case WM_TIMER:
      if (toolPage == 2) InvalidateRect(window, nullptr, FALSE);
      return 0;
    case WM_MOUSEWHEEL:
      if (toolPage == 0 || toolPage == 1) {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam) / 120;
        const int step = static_cast<int>(toolPage == 0 ? dp(36) : dp(48));
        int& scroll = toolPage == 0 ? festivalScroll : historyScroll;
        scroll = std::max(0, scroll - delta * step);
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
      Gdiplus::Bitmap buffer(client.right, client.bottom, PixelFormat32bppPARGB);
      Graphics graphics(&buffer);
      graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
      paintTools(graphics, client.right, client.bottom);
      Graphics screen(dc);
      screen.DrawImage(&buffer, 0, 0);
      EndPaint(window, &paint);
      return 0;
    }
    case WM_LBUTTONDOWN: {
      const int id = hitTest(toolHits, static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)));
      if (id == 1) {
        ShowWindow(window, SW_HIDE);
        invalidateCalendar();
        return 0;
      }
      if (id >= 10 && id < 13) {
        toolPage = id - 10;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
      }
      if (id == 21 || id == 22) {
        stepDate(toolMonth, toolDay, id == 22 ? 1 : -1, true);
        historyScroll = 0;
      } else if (id == 23 || id == 24) {
        stepDate(toolMonth, toolDay, id == 24 ? 1 : -1, false);
        historyScroll = 0;
      }
      else if (id == 25 || id == 26) {
        toolYear = std::max(1901, std::min(2100, toolYear + (id == 26 ? 1 : -1)));
        festivalScroll = 0;
      }
      else if (static_cast<short>(HIWORD(lParam)) < static_cast<int>(dp(48))) {
        ReleaseCapture();
        SendMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
      }
      InvalidateRect(window, nullptr, FALSE);
      return 0;
    }
    case WM_CLOSE:
      ShowWindow(window, SW_HIDE);
      invalidateCalendar();
      return 0;
    case WM_DESTROY:
      toolsWindow = nullptr;
      return 0;
    default:
      return DefWindowProcW(window, message, wParam, lParam);
  }
}

void addCurrentNote() {
  if (!noteEdit) return;
  wchar_t buffer[80]{};
  GetWindowTextW(noteEdit, buffer, 80);
  std::wstring name = buffer;
  while (!name.empty() && name.back() == L' ') name.pop_back();
  if (name.empty()) return;
  Mark mark;
  mark.kind = 2;
  mark.month = noteMonth;
  mark.day = noteDay;
  mark.name = name;
  marks.push_back(mark);
  noteSelected = static_cast<int>(marks.size()) - 1;
  SetWindowTextW(noteEdit, L"");
  saveMarks();
  invalidateCalendar();
}

LRESULT CALLBACK notesProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_CREATE: {
      noteEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, window, reinterpret_cast<HMENU>(1), GetModuleHandleW(nullptr), nullptr);
      HFONT font = CreateFontW(-std::max(12, static_cast<int>(dp(14))), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
      SendMessageW(noteEdit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
      SendMessageW(noteEdit, EM_SETLIMITTEXT, 40, 0);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_CTLCOLOREDIT:
      SetBkColor(reinterpret_cast<HDC>(wParam), RGB(255, 255, 255));
      SetTextColor(reinterpret_cast<HDC>(wParam), RGB(40, 40, 40));
      return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    case WM_PAINT: {
      PAINTSTRUCT paint;
      HDC dc = BeginPaint(window, &paint);
      RECT client{};
      GetClientRect(window, &client);
      Gdiplus::Bitmap buffer(client.right, client.bottom, PixelFormat32bppPARGB);
      Graphics graphics(&buffer);
      graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
      paintNotes(graphics, client.right, client.bottom);
      Graphics screen(dc);
      screen.DrawImage(&buffer, 0, 0);
      EndPaint(window, &paint);
      return 0;
    }
    case WM_LBUTTONDOWN: {
      const int id = hitTest(noteHits, static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)));
      if (id == 1) {
        ShowWindow(window, SW_HIDE);
        invalidateCalendar();
        return 0;
      }
      if (id >= 40 && id <= 43 && noteEdit) SetWindowTextW(noteEdit, presetTitle(id - 40));
      else if (id == 33 || id == 34) stepDate(noteMonth, noteDay, id == 34 ? 1 : -1, true);
      else if (id == 35 || id == 36) stepDate(noteMonth, noteDay, id == 36 ? 1 : -1, false);
      else if (id == 37) addCurrentNote();
      else if (id == 38 && noteSelected >= 0 && noteSelected < static_cast<int>(marks.size())) {
        marks.erase(marks.begin() + noteSelected);
        noteSelected = -1;
        saveMarks();
        invalidateCalendar();
      } else if (id >= 100) noteSelected = id - 100;
      else if (static_cast<short>(HIWORD(lParam)) < static_cast<int>(dp(48))) {
        ReleaseCapture();
        SendMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
      }
      InvalidateRect(window, nullptr, FALSE);
      return 0;
    }
    case WM_CLOSE:
      ShowWindow(window, SW_HIDE);
      invalidateCalendar();
      return 0;
    case WM_DESTROY:
      noteEdit = nullptr;
      notesWindow = nullptr;
      return 0;
    default:
      return DefWindowProcW(window, message, wParam, lParam);
  }
}

LRESULT CALLBACK alertProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_LBUTTONDOWN:
      ShowWindow(window, SW_HIDE);
      KillTimer(window, 1);
      if (calendarIsConcealed()) restoreCalendar();
      return 0;
    case WM_TIMER:
      if (alertGif && advanceAlertGif()) InvalidateRect(window, nullptr, FALSE);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT paint;
      HDC dc = BeginPaint(window, &paint);
      RECT client{};
      GetClientRect(window, &client);
      Gdiplus::Bitmap buffer(client.right, client.bottom, PixelFormat32bppPARGB);
      Graphics graphics(&buffer);
      graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
      paintAlert(graphics, client.right, client.bottom);
      Graphics screen(dc);
      screen.DrawImage(&buffer, 0, 0);
      EndPaint(window, &paint);
      return 0;
    }
    case WM_CLOSE:
      KillTimer(window, 1);
      ShowWindow(window, SW_HIDE);
      return 0;
    case WM_DESTROY:
      alertWindow = nullptr;
      return 0;
    default:
      return DefWindowProcW(window, message, wParam, lParam);
  }
}

LRESULT CALLBACK dockProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_LBUTTONDOWN:
      toggleCalendarFromDock();
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT paint;
      HDC dc = BeginPaint(window, &paint);
      RECT client{};
      GetClientRect(window, &client);
      Gdiplus::Bitmap buffer(std::max(1L, client.right), std::max(1L, client.bottom), PixelFormat32bppPARGB);
      Graphics graphics(&buffer);
      graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
      paintDock(graphics, client.right, client.bottom);
      Graphics screen(dc);
      screen.DrawImage(&buffer, 0, 0);
      EndPaint(window, &paint);
      return 0;
    }
    case WM_DESTROY:
      dockWindow = nullptr;
      return 0;
    default:
      return DefWindowProcW(window, message, wParam, lParam);
  }
}

void ensureClass(const wchar_t* name, WNDPROC proc) {
  WNDCLASSW windowClass{};
  if (GetClassInfoW(GetModuleHandleW(nullptr), name, &windowClass)) return;
  windowClass.lpfnWndProc = proc;
  windowClass.hInstance = GetModuleHandleW(nullptr);
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.lpszClassName = name;
  RegisterClassW(&windowClass);
}

}

void bindCalendarWindow(HWND window) { calendarWindow = window; }

bool calendarIsConcealed() {
  return !calendarWindow || !IsWindow(calendarWindow) || !IsWindowVisible(calendarWindow);
}

void applyWindowOpacity() {
  if (!calendarWindow || !IsWindow(calendarWindow)) return;
  const int percent = std::max(40, std::min(100, calendarSettings().windowOpacity));
  LONG style = GetWindowLongW(calendarWindow, GWL_EXSTYLE);
  SetWindowLongW(calendarWindow, GWL_EXSTYLE, style | WS_EX_LAYERED);
  SetLayeredWindowAttributes(calendarWindow, 0, static_cast<BYTE>(percent * 255 / 100), LWA_ALPHA);
}

BOOL CALLBACK matchTrayClock(HWND child, LPARAM param) {
  wchar_t name[64]{};
  if (GetClassNameW(child, name, 64) == 0) return TRUE;
  if (wcscmp(name, L"TrayClockWClass") != 0) return TRUE;
  *reinterpret_cast<HWND*>(param) = child;
  return FALSE;
}

bool systemClockPlace(RECT& place) {
  HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
  if (!tray) return false;
  HWND clock = nullptr;
  EnumChildWindows(tray, matchTrayClock, reinterpret_cast<LPARAM>(&clock));
  if (!clock || !GetWindowRect(clock, &place)) return false;
  return place.right - place.left >= 8 && place.bottom - place.top >= 8;
}

void updateDockClock() {
  if (!calendarSettings().dockClock) {
    if (dockWindow) ShowWindow(dockWindow, SW_HIDE);
    return;
  }
  RECT place{};
  if (!systemClockPlace(place)) {
    APPBARDATA bar{};
    bar.cbSize = sizeof(bar);
    if (!SHAppBarMessage(ABM_GETTASKBARPOS, &bar)) return;
    place = bar.rc;
    if (bar.uEdge == ABE_LEFT || bar.uEdge == ABE_RIGHT) {
      const int height = 72;
      place.top = bar.rc.bottom - height;
      if (place.top < bar.rc.top) place.top = bar.rc.top;
    } else {
      const int width = 190;
      place.left = bar.rc.right - width;
      if (place.left < bar.rc.left) place.left = bar.rc.left;
    }
  }
  if (!dockWindow) {
    ensureClass(L"YxCalendarDock", dockProc);
    dockWindow = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"YxCalendarDock", L"", WS_POPUP, place.left, place.top, place.right - place.left, place.bottom - place.top, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  }
  if (!dockWindow) return;
  SetWindowPos(dockWindow, HWND_TOPMOST, place.left, place.top, place.right - place.left, place.bottom - place.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  InvalidateRect(dockWindow, nullptr, FALSE);
}

void showDuePopup(const wchar_t* title, const wchar_t* detail, const wchar_t* gifName) {
  alertTitle = title ? title : L"";
  alertDetail = detail ? detail : L"";
  alertGif = gifName && gifName[0];
  readScale();
  const FittedWindow fitted = fitDesign(260.f, 118.f);
  const int width = fitted.width;
  const int height = fitted.height;
  RECT work{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  const int x = work.right - width - 16;
  const int y = work.bottom - height - 16;
  if (!alertWindow) {
    ensureClass(L"YxCalendarAlert", alertProc);
    alertWindow = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"YxCalendarAlert", L"", WS_POPUP, x, y, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  }
  if (!alertWindow) return;
  SetWindowPos(alertWindow, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);
  if (alertGif) {
    ensureAlertGif(gifName);
    SetTimer(alertWindow, 1, 40, nullptr);
  } else KillTimer(alertWindow, 1);
  InvalidateRect(alertWindow, nullptr, FALSE);
}

void applyToolsNotesSize() {
  if (!toolsWindow && !notesWindow && !alertWindow) return;
  readScale();
  const FittedWindow panel = fitDesign(kPanelWidth, kPanelHeight);
  moveFittedWindow(toolsWindow, panel);
  moveFittedWindow(notesWindow, panel);
  if (alertWindow) moveFittedWindow(alertWindow, fitDesign(260.f, 118.f));
}

void showToolsWindow(HWND owner) {
  readScale();
  if (!toolsWindow) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    toolYear = now.wYear;
    toolMonth = now.wMonth;
    toolDay = now.wDay;
    ensureClass(L"YxCalendarTools", toolsProc);
    const FittedWindow fitted = fitDesign(kPanelWidth, kPanelHeight);
    uiScale = fitted.scale;
    const int width = fitted.width;
    const int height = fitted.height;
    int x = 0;
    int y = 0;
    placeNear(owner, width, height, x, y);
    toolsWindow = CreateWindowExW(WS_EX_TOOLWINDOW, L"YxCalendarTools", tr(L"工具", L"Tools"), WS_POPUP, x, y, width, height, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
  }
  if (!toolsWindow) return;
  applyToolsNotesSize();
  ShowWindow(toolsWindow, SW_SHOW);
  SetForegroundWindow(toolsWindow);
  invalidateCalendar();
}

void showNotesWindow(HWND owner) {
  readScale();
  loadMarks();
  if (!notesWindow) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    noteMonth = now.wMonth;
    noteDay = now.wDay;
    ensureClass(L"YxCalendarNotes", notesProc);
    const FittedWindow fitted = fitDesign(kPanelWidth, kPanelHeight);
    uiScale = fitted.scale;
    const int width = fitted.width;
    const int height = fitted.height;
    int x = 0;
    int y = 0;
    placeNear(owner, width, height, x, y);
    notesWindow = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, L"YxCalendarNotes", tr(L"记录", L"Notes"), WS_POPUP | WS_CLIPCHILDREN, x, y, width, height, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
  }
  if (!notesWindow) return;
  applyToolsNotesSize();
  ShowWindow(notesWindow, SW_SHOW);
  SetForegroundWindow(notesWindow);
  invalidateCalendar();
}

bool toolsWindowVisible() { return toolsWindow && IsWindowVisible(toolsWindow); }

bool notesWindowVisible() { return notesWindow && IsWindowVisible(notesWindow); }

std::wstring personalMark(int month, int day) {
  if (!marksLoaded) loadMarks();
  std::wstring text;
  for (const Mark& mark : marks) {
    if (mark.month != month || mark.day != day) continue;
    if (!text.empty()) text += L" ";
    text += markLabel(mark);
  }
  return text;
}
