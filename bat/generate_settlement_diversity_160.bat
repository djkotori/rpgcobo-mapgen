@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0\.."
set "EXE=%CD%\build\Release\mapimggen.exe"
if not exist "%EXE%" (
    echo ERROR: Build first: bat\build.bat
    exit /b 1
)
set "FIRST_SEED=1"
if not "%~1"=="" set "FIRST_SEED=%~1"
for /f %%I in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set "SESSION=%%I"
set "OUT_DIR=%CD%\out\%SESSION%_settlement_diversity"
set /a FAIL=0
rem Hold orientation and city style fixed: only the seed changes the terrain.
for %%T in (town village) do (
    for %%S in (1 5 6) do (
        for /L %%N in (0,1,7) do (
            set /a CURRENT_SEED=FIRST_SEED+%%N
            "%EXE%" -type %%T -w 160 -d 160 -seed !CURRENT_SEED! -p settlementSite=%%S -p siteRotation=0 -p townCityType=0 -dir "%OUT_DIR%" -out %%T_%%S_seed!CURRENT_SEED! --png --report --plan --strict >nul
            if errorlevel 1 (set /a FAIL+=1) else (echo OK: %%T_%%S_seed!CURRENT_SEED!.png)
        )
    )
)
echo 1=coast 5=lake 6=oasis; fixed orientation, 8 seeds per site/type
echo Output: %OUT_DIR%
if %FAIL% gtr 0 exit /b 2
exit /b 0
