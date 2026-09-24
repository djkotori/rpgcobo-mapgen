@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd %~dp0\..

set EXE=%CD%\build\Release\mapimggen.exe
if not exist "%EXE%" (
    echo ERROR: executable not found: %EXE%
    echo Build first: bat\build.bat
    exit /b 1
)

set WIDTH=160
set DEPTH=160
set WEALTH_ARG=
set WEALTH_TAG=
set DEFENSE_ARG=
set DEFENSE_TAG=
if not "%~1"=="" (
    set WEALTH_ARG=-p countryWealth=%~1
    set WEALTH_TAG=_wealth_%~1
    echo Fixed country wealth: %~1
)
if not "%~2"=="" (
    set DEFENSE_ARG=-p outerWallDefenseStrength=%~2
    set DEFENSE_TAG=_defense_%~2
    echo Fixed outer wall defense: %~2
)
for /f %%I in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set SESSION=%%I
set OUT_DIR=%CD%\out\%SESSION%_castle!WEALTH_TAG!!DEFENSE_TAG!
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
set /a BASE_SEED=%RANDOM% * 32768 + %RANDOM%

set FAIL=0
for /L %%I in (1,1,30) do (
    set /a SEED=!BASE_SEED! + %%I * 7919
    "%EXE%" -type castle -w %WIDTH% -d %DEPTH% -seed !SEED! !WEALTH_ARG! !DEFENSE_ARG! -dir "%OUT_DIR%" -out %%I --png --report --plan --strict >nul
    if errorlevel 1 (set /a FAIL+=1) else (echo [%%I/30] OK -^> %%I.png)
)
if %FAIL% gtr 0 exit /b 2
echo Completed successfully: %OUT_DIR%
exit /b 0