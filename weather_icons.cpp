#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <gdiplus.h>

#include "log.hpp"
#include "paths.hpp"
#include "weather_icons.hpp"

#include <cmath>
#include <map>
#include <string>

using namespace Gdiplus;

namespace {

struct CachedIcon {
  Bitmap* image = nullptr;
};

std::map<int, CachedIcon> cache;

bool fileExists(const std::wstring& path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

Bitmap* loadBitmap(const std::wstring& path) {
  Bitmap file(path.c_str());
  if (file.GetLastStatus() != Ok || file.GetWidth() == 0) return nullptr;
  auto* copy = new Bitmap(static_cast<INT>(file.GetWidth()), static_cast<INT>(file.GetHeight()), PixelFormat32bppPARGB);
  Graphics graphics(copy);
  graphics.DrawImage(&file, 0, 0, static_cast<INT>(file.GetWidth()), static_cast<INT>(file.GetHeight()));
  if (copy->GetLastStatus() != Ok) {
    delete copy;
    return nullptr;
  }
  return copy;
}

int mappedCode(int wmoCode) {
  if (wmoCode < 0) return -1;
  if (wmoCode == 0) return 100;
  if (wmoCode == 1) return 102;
  if (wmoCode == 2) return 101;
  if (wmoCode == 3) return 104;
  if (wmoCode == 45 || wmoCode == 48) return 501;
  if (wmoCode >= 51 && wmoCode <= 57) return 309;
  if (wmoCode == 61 || wmoCode == 80) return 305;
  if (wmoCode == 63 || wmoCode == 81) return 306;
  if (wmoCode == 65 || wmoCode == 82) return 307;
  if (wmoCode == 66 || wmoCode == 67) return 313;
  if (wmoCode == 71 || wmoCode == 77 || wmoCode == 85) return 400;
  if (wmoCode == 73) return 401;
  if (wmoCode == 75 || wmoCode == 86) return 402;
  if (wmoCode >= 95) return 302;
  return 999;
}

Bitmap* iconFor(int code) {
  const auto found = cache.find(code);
  if (found != cache.end()) return found->second.image;
  wchar_t name[16];
  std::swprintf(name, 16, L"%d.png", code);
  const std::wstring primary = dataPath(L"icons\\") + name;
  const std::wstring backup = dataPath(L"icons\\icons-backup\\") + name;
  std::wstring path;
  if (fileExists(primary)) path = primary;
  else if (fileExists(backup)) path = backup;
  else logMissingResource(primary);
  CachedIcon entry;
  if (!path.empty()) entry.image = loadBitmap(path);
  const auto inserted = cache.emplace(code, entry);
  return inserted.first->second.image;
}

Color colorOf(BYTE red, BYTE green, BYTE blue) {
  return Color(255, red, green, blue);
}

void drawFallback(Graphics& graphics, float x, float y, float diameter, int wmoCode) {
  const bool snow = (wmoCode >= 71 && wmoCode <= 77) || wmoCode == 85 || wmoCode == 86;
  const bool rain = (wmoCode >= 51 && wmoCode <= 67) || (wmoCode >= 80 && wmoCode <= 82) || wmoCode >= 95;
  const bool cloud = wmoCode == 2 || wmoCode == 3 || wmoCode == 45 || wmoCode == 48 || rain || snow;
  if (wmoCode < 0 || wmoCode <= 2) {
    const float cx = x + diameter / 2.f;
    const float cy = y + diameter / 2.f;
    const float radius = diameter * 0.28f;
    SolidBrush glow(colorOf(255, 186, 46));
    graphics.FillEllipse(&glow, cx - radius, cy - radius, radius * 2.f, radius * 2.f);
  }
  if (cloud) {
    SolidBrush brush(colorOf(214, 224, 236));
    graphics.FillEllipse(&brush, x + diameter * 0.16f, y + diameter * 0.38f, diameter * 0.42f, diameter * 0.42f);
    graphics.FillEllipse(&brush, x + diameter * 0.40f, y + diameter * 0.24f, diameter * 0.48f, diameter * 0.48f);
    graphics.FillEllipse(&brush, x + diameter * 0.12f, y + diameter * 0.52f, diameter * 0.76f, diameter * 0.32f);
  }
}

}

bool drawConditionIcon(Graphics& graphics, float x, float y, float size, int conditionCode, int wmoCode) {
  const int code = conditionCode >= 0 ? conditionCode : mappedCode(wmoCode);
  if (code >= 0) {
    if (Bitmap* image = iconFor(code)) {
      const InterpolationMode previous = graphics.GetInterpolationMode();
      graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
      graphics.DrawImage(image, RectF(x, y, size, size));
      graphics.SetInterpolationMode(previous);
      return true;
    }
  }
  drawFallback(graphics, x, y, size, wmoCode);
  return false;
}
