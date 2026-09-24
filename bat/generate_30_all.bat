@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd %~dp0\..

set EXE=%CD%\build\Release\mapimggen.exe
if not exist "%EXE%" (
    echo ERROR: executable not found: %EXE%
    echo Build first: bat\build.bat
    exit /b 1
)

for /f %%I in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set SESSION=%%I
set ROOT_OUT=%CD%\out\%SESSION%_all
if not exist "%ROOT_OUT%" mkdir "%ROOT_OUT%"
set /a BASE_SEED=%RANDOM% * 32768 + %RANDOM%
set FAIL=0

call :generate world 128 128
call :generate dungeon 128 128
call :generate cave 160 160
call :generate town 160 160
call :generate village 160 160
call :generate castle 160 160

echo.
if !FAIL! GTR 0 (
    echo Completed with !FAIL! failures. Output: !ROOT_OUT!
    exit /b 2
)
echo Completed successfully. Output: %ROOT_OUT%
exit /b 0

:generate
set TYPE=%~1
set WIDTH=%~2
set DEPTH=%~3
set OUT_DIR=%ROOT_OUT%\%TYPE%
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
echo === %TYPE% %WIDTH%x%DEPTH% ===
for /L %%I in (1,1,30) do (
    set /a SEED=!BASE_SEED! + %%I * 7919 + !RANDOM!
    "%EXE%" -type %TYPE% -w %WIDTH% -d %DEPTH% -seed !SEED! -dir "%OUT_DIR%" -out %%I --png --data --report --plan --strict >nul
    if errorlevel 1 (
        set /a FAIL+=1
        echo [%%I/30] FAILED seed=!SEED!
    ) else (
        echo [%%I/30] OK seed=!SEED!
    )
)
exit /b 0
