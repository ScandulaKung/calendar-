#pragma once

#include <string>

bool loadChinaCityList(const std::wstring& path);
std::wstring chineseCityName(double latitude, double longitude, const std::wstring& hint);
bool chinaCityCenter(const std::wstring& province, const std::wstring& city, double& latitude, double& longitude);
