@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0\.."
set "EXE=%CD%\build\Release\mapimggen.exe"
if not exist "%EXE%" (
    echo ERROR: Build first: bat\build.bat
    exit /b 1
)
set "SEED=4242"
if not "%~1"=="" set "SEED=%~1"
for /f %%I in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set "SESSION=%%I"
set "OUT_DIR=%CD%\out\%SESSION%_settlement_sites"
set /a FAIL=0
for %%T in (town village) do (
    for /L %%S in (0,1,6) do (
        "%EXE%" -type %%T -w 160 -d 160 -seed %SEED% -p settlementSite=%%S -p siteRotation=0 -dir "%OUT_DIR%" -out %%T_%%S --png --report --plan --strict >nul
        if errorlevel 1 (set /a FAIL+=1) else (echo OK: %%T_%%S.png)
    )
)
echo 0=plains 1=coast 2=mountain_valley 3=forest 4=river 5=lake 6=oasis
echo Output: %OUT_DIR%
if %FAIL% gtr 0 exit /b 2
exit /b 0
