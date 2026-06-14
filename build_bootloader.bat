@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

echo Building Breezedude UF2 bootloader...

if exist "%USERPROFILE%\.platformio\packages\toolchain-gccarmnoneeabi\bin\arm-none-eabi-gcc.exe" (
  set "PATH=%USERPROFILE%\.platformio\packages\toolchain-gccarmnoneeabi\bin;%PATH%"
)

set "BOARD=breezedude"
set "BUILD_DIR=build\%BOARD%"
set "DEST_DIR=%~dp0..\breezedude_WindSensor\bootloader"

rem Optional manual overrides (set in shell before calling this script):
rem   UF2_VERSION_OVERRIDE         -> full internal UF2 version string
rem   UF2_VERSION_PUBLIC_OVERRIDE  -> short version shown in VERSIONS.TXT

if exist "build" rmdir /s /q "build"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

set "UF2BASE=v4.2.0"
set "UF2VER=%UF2BASE%"
for /f %%i in ('git rev-parse --short HEAD 2^>nul') do set "GITSHA=%%i"
if defined GITSHA set "UF2VER=%UF2BASE%-0-g%GITSHA%"
for /f %%i in ('git describe --dirty --always 2^>nul') do set "GITDESC=%%i"
if defined GITDESC if /I not "%GITDESC%"=="%GITDESC:-dirty=%" set "UF2VER=%UF2VER%-dirty"

set "UF2PUBLIC=%UF2BASE%"
if defined UF2_VERSION_PUBLIC_OVERRIDE set "UF2PUBLIC=%UF2_VERSION_PUBLIC_OVERRIDE%"

echo #define UF2_VERSION_BASE ^"%UF2VER%^" > "%BUILD_DIR%\uf2_version.h"
echo #define UF2_VERSION_PUBLIC ^"%UF2PUBLIC%^" >> "%BUILD_DIR%\uf2_version.h"
if defined UF2_VERSION_OVERRIDE echo #define UF2_VERSION_OVERRIDE ^"%UF2_VERSION_OVERRIDE%^" >> "%BUILD_DIR%\uf2_version.h"

make BOARD=%BOARD%
if errorlevel 1 exit /b %errorlevel%

set "BOOT_BIN="
set "UPDATE_UF2="
set "LEGACY_MIN_UF2="
set "LAYOUTV2_UF2="

for %%f in ("%BUILD_DIR%\bootloader-%BOARD%-*.bin") do set "BOOT_BIN=%%~ff"
for %%f in ("%BUILD_DIR%\update-bootloader-%BOARD%-*.uf2") do set "UPDATE_UF2=%%~ff"
for %%f in ("%BUILD_DIR%\update-legacy-bootloader-%BOARD%-*.uf2") do set "LEGACY_MIN_UF2=%%~ff"
for %%f in ("%BUILD_DIR%\update-layoutv2-bootloader-%BOARD%-*.uf2") do set "LAYOUTV2_UF2=%%~ff"

if not defined BOOT_BIN (
  echo ERROR: bootloader binary not found
  exit /b 1
)
if not defined UPDATE_UF2 (
  echo ERROR: update UF2 not found
  exit /b 1
)

if not exist "%DEST_DIR%" mkdir "%DEST_DIR%"

copy /y "%BOOT_BIN%" "%DEST_DIR%\bootloader-breezedude-16k.bin" >nul
copy /y "%UPDATE_UF2%" "%DEST_DIR%\update-bootloader-breezedude.uf2" >nul

if defined LEGACY_MIN_UF2 (
  copy /y "%LEGACY_MIN_UF2%" "%DEST_DIR%\update-bootloader-breezedude-16k-legacy-minimal.uf2" >nul
)

if defined LAYOUTV2_UF2 (
  copy /y "%LAYOUTV2_UF2%" "%DEST_DIR%\update-bootloader-breezedude-layout-v2-migration.uf2" >nul
)

if exist "%DEST_DIR%\update-bootloader-breezedude-16k-legacy.uf2" del /q "%DEST_DIR%\update-bootloader-breezedude-16k-legacy.uf2"
if exist "%DEST_DIR%\update-bootloader-breezedude-from-v3.16.uf2" del /q "%DEST_DIR%\update-bootloader-breezedude-from-v3.16.uf2"
if exist "%DEST_DIR%\update-bootloader-breezedude-16k.uf2" del /q "%DEST_DIR%\update-bootloader-breezedude-16k.uf2"

echo.
echo Created user-facing artifacts in "%DEST_DIR%":
echo   - update-bootloader-breezedude.uf2 (bootloader update AND v3.16 migration - same file for both)
if defined LAYOUTV2_UF2 echo   - update-bootloader-breezedude-layout-v2-migration.uf2 (one-time OTA layout v1 -^> v2 migration for existing v4.1.x devices)
if defined LEGACY_MIN_UF2 echo   - update-bootloader-breezedude-16k-legacy-minimal.uf2 (debug only)

for %%f in ("%BUILD_DIR%\bootloader-breezedude*.elf") do (
  echo.
  arm-none-eabi-size "%%f"
)

echo.
echo Done.
endlocal
