#pragma once

#include <string>

// Include windows.h before this header. calendar_app gets it through settings.hpp.

struct ForecastDay {
  int year = 0;
  int month = 0;
  int day = 0;
  int weekday = 0;
  int code = -1;
  int high = 0;
  int low = 0;
  int windSpeed = 0;
  int windDirection = 0;
  int conditionCode = -1;
  std::wstring conditionText;
};

struct WeatherSnapshot {
  bool ready = false;
  int code = -1;
  int conditionCode = -1;
  double temperature = 0;
  int humidity = -1;
  int windSpeed = 0;
  int windDirection = 0;
  int aqi = -1;
  int updatedHour = 0;
  int updatedMinute = 0;
  int warningCount = 0;
  int warningRed = -1;
  int warningGreen = -1;
  int warningBlue = -1;
  std::wstring city;
  std::wstring conditionText;
  std::wstring warningText;
  std::wstring warningNote;
  std::wstring sunrise;
  std::wstring sunset;
  std::wstring moonrise;
  std::wstring moonset;
  std::wstring moonPhase;
  ForecastDay days[7]{};
  int dayCount = 0;
};

inline void warningRgb(const WeatherSnapshot& weather, int& red, int& green, int& blue) {
  const int sum = weather.warningRed + weather.warningGreen + weather.warningBlue;
  if (weather.warningRed < 0 || weather.warningGreen < 0 || weather.warningBlue < 0 || sum > 680) {
    red = 214;
    green = 148;
    blue = 32;
    return;
  }
  red = weather.warningRed;
  green = weather.warningGreen;
  blue = weather.warningBlue;
}

void startWeatherUpdates();
void stopWeatherUpdates();
void requestWeatherRefresh();
WeatherSnapshot currentWeather();
const wchar_t* weatherDescription(int code);
std::wstring windDescription(int speedKmh, int directionDegrees);
const wchar_t* airQualityLabel(int aqi);

void toggleWeatherPanel(HWND owner);
void applyWeatherPanelSize();
bool weatherPanelVisible();
void invalidateWeatherPanel();
