@echo off
setlocal
cd /d "%~dp0"

rem Bottom-bar icon size for Plans, Notes, Tools, Settings:
rem   calendar_app.cpp  paintWindow()  iconBox = 27.2f * scale
rem   labelGap is the space between that icon and its caption.
rem   drawTabIcon() draws the four shapes to fill iconBox.
rem Change those lines, save, then run this script.

where cmake >nul 2>&1
if errorlevel 1 (
  echo cmake was not found on PATH.
  exit /b 1
)

taskkill /IM calendar_calc.exe /F >nul 2>&1

cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1

cmake --build build --target calendar_calc
if errorlevel 1 exit /b 1

if not exist release mkdir release
if not exist release\local mkdir release\local
copy /Y build\calendar_calc.exe release\calendar_calc.exe >nul
if errorlevel 1 exit /b 1
copy /Y changelog.txt release\changelog.txt >nul
if errorlevel 1 exit /b 1
copy /Y calendar_settings.ini release\calendar_settings.ini >nul
if errorlevel 1 exit /b 1
copy /Y history_cache.txt release\history_cache.txt >nul
if errorlevel 1 exit /b 1
copy /Y qweather_private.pem release\qweather_private.pem >nul
if errorlevel 1 exit /b 1
copy /Y qweather_public.pem release\qweather_public.pem >nul
if errorlevel 1 exit /b 1
copy /Y qweather_quota.txt release\qweather_quota.txt >nul
if errorlevel 1 exit /b 1
copy /Y weather-conditions.csv release\weather-conditions.csv >nul
if errorlevel 1 exit /b 1
copy /Y weather-conditions.csv release\local\weather-conditions.csv >nul
if errorlevel 1 exit /b 1
copy /Y LocationList-master\China-City-List-latest.csv release\local\China-City-List-latest.csv >nul
if errorlevel 1 exit /b 1
copy /Y app.ico release\app.ico >nul
if errorlevel 1 exit /b 1
if exist timeisup.gif copy /Y timeisup.gif release\timeisup.gif >nul
if errorlevel 1 exit /b 1
if exist Alarm.gif copy /Y Alarm.gif release\Alarm.gif >nul
if errorlevel 1 exit /b 1
if exist personal_dates.txt copy /Y personal_dates.txt release\personal_dates.txt >nul
for /f "delims=" %%G in ('where g++') do set "MINGW_BIN=%%~dpG"
if defined MINGW_BIN (
  copy /Y "%MINGW_BIN%libstdc++-6.dll" release\ >nul
  copy /Y "%MINGW_BIN%libgcc_s_seh-1.dll" release\ >nul
)
robocopy Sounds release\Sounds /E /NFL /NDL /NJH /NJS /NP >nul
if %ERRORLEVEL% GEQ 8 exit /b %ERRORLEVEL%
robocopy icons release\icons /E /NFL /NDL /NJH /NJS /NP >nul
if %ERRORLEVEL% GEQ 8 exit /b %ERRORLEVEL%
robocopy wiki_pages release\wiki_pages /E /XF dl*.tmp /NFL /NDL /NJH /NJS /NP >nul
if %ERRORLEVEL% GEQ 8 exit /b %ERRORLEVEL%

echo Built build\calendar_calc.exe
echo Published to release, including calendar_settings.ini
exit /b 0
