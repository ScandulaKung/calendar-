#pragma once

#include <gdiplus.h>
#include <string>

struct Palette {
  unsigned char background[3];
  unsigned char text[3];
  unsigned char muted[3];
  unsigned char card[3];
  unsigned char bar[3];
  unsigned char line[3];
  unsigned char weekend[3];
  unsigned char dim[3];
};

Palette currentPalette();
void drawThemedBackground(Gdiplus::Graphics& graphics, int width, int height);
void setBackgroundFrame(int width, int height);
bool pollBackgroundShuffle();
bool backgroundUsesMotion();
bool advanceBackground();
bool backgroundVideoTooLarge(const std::wstring& path);
