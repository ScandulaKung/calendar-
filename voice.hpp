#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>

void pollVoiceClock();
void showPlanWindow(HWND owner);
void applyPlanWindowSize();
bool planWindowVisible();
void refreshPlanWindow();
