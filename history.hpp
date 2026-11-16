#pragma once

#include <string>
#include <vector>

struct HistorySnapshot {
  bool ready = false;
  int month = 0;
  int day = 0;
  int years[3]{};
  std::wstring texts[3];
  int count = 0;
};

struct HistoryEntry {
  int year = 0;
  std::wstring text;
};

void startHistoryUpdates();
void stopHistoryUpdates();
HistorySnapshot currentHistory();
std::vector<HistoryEntry> historyForDate(int month, int day);
