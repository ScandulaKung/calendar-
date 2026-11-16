#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>

#include <string>

void bindCalendarWindow(HWND window);
void restoreCalendar();
void toggleCalendarFromDock();
bool calendarIsConcealed();
void applyWindowOpacity();
void updateDockClock();
void showDuePopup(const wchar_t* title, const wchar_t* detail, const wchar_t* gifName = nullptr);
void showToolsWindow(HWND owner);
void showNotesWindow(HWND owner);
void applyToolsNotesSize();
bool toolsWindowVisible();
bool notesWindowVisible();
std::wstring personalMark(int month, int day);
