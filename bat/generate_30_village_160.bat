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
for /f %%I in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set SESSION=%%I
set OUT_DIR=%CD%\out\%SESSION%_village
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
set /a BASE_SEED=%RANDOM% * 32768 + %RANDOM%

set FAIL=0
for /L %%I in (1,1,30) do (
    set /a SEED=!BASE_SEED! + %%I * 7919
    "%EXE%" -type village -w %WIDTH% -d %DEPTH% -seed !SEED! -dir "%OUT_DIR%" -out %%I --png --report --plan --strict >nul
    if errorlevel 1 (set /a FAIL+=1) else (echo [%%I/30] OK -^> %%I.png)
)
if %FAIL% gtr 0 exit /b 2
echo Completed successfully: %OUT_DIR%
exit /b 0
