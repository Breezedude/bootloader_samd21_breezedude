@echo off
REM Host test for src/ota_swap.c - the firmware swap of the bootloader with a
REM power cut at every flash erase and page write, see test_ota_swap.c.
REM
REM It compiles the real bootloader source natively against a flash model; the
REM uf2.h next to this file stands in for inc/uf2.h. Needs Visual Studio's cl.exe.
REM
REM Usage:  run.bat            (from a normal cmd prompt, finds vcvars itself)
REM         run.bat nosetup    (from a Developer Command Prompt)

setlocal
set HERE=%~dp0
set WORK=%HERE%build

if /I "%~1"=="nosetup" goto :build
for %%V in (
  "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
) do if exist %%V ( call %%V >nul 2>&1 & goto :build )
echo Could not find vcvars64.bat - run this from a Developer Command Prompt
echo and pass "nosetup", or adjust the paths above.
exit /b 1

:build
if not exist "%WORK%" mkdir "%WORK%"

copy /Y "%HERE%..\..\..\src\ota_swap.c" "%WORK%\ota_swap.c" >nul
copy /Y "%HERE%..\..\..\inc\ota_swap.h" "%WORK%\ota_swap.h" >nul
copy /Y "%HERE%uf2.h"                   "%WORK%\uf2.h"      >nul
copy /Y "%HERE%test_ota_swap.c"         "%WORK%\"           >nul

pushd "%WORK%"
cl /nologo /O2 /W3 /utf-8 /I. test_ota_swap.c ota_swap.c /Fe:test_ota_swap.exe >nul
if errorlevel 1 (
  echo COMPILE FAILED
  cl /nologo /O2 /W3 /utf-8 /I. test_ota_swap.c ota_swap.c /Fe:test_ota_swap.exe
  popd
  exit /b 1
)
REM .\ prefix: some shells set NoDefaultCurrentDirectoryInExePath
.\test_ota_swap.exe
set RC=%ERRORLEVEL%
popd
exit /b %RC%
