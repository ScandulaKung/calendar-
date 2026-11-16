#pragma once

#include <windows.h>
#include <gdiplus.h>

bool drawConditionIcon(Gdiplus::Graphics& graphics, float x, float y, float size, int conditionCode, int wmoCode);
