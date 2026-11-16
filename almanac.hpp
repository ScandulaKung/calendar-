#pragma once

#include <string>

struct Almanac {
  std::wstring lunar;
  std::wstring cycle;
  std::wstring suitable;
  std::wstring avoid;
};

Almanac almanacFor(int year, int month, int day);
