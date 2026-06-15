@echo off
setlocal

set PORT=%1
if "%PORT%"=="" set PORT=COM3

set SCRIPT_DIR=%~dp0
if exist "%SCRIPT_DIR%firmware" (
  set PACKAGE_ROOT=%SCRIPT_DIR%
) else (
  for %%I in ("%SCRIPT_DIR%..") do set PACKAGE_ROOT=%%~fI\
)

set FW=%PACKAGE_ROOT%firmware
set PY=python

if not exist "%FW%\bootloader.bin" (
  echo Missing firmware file: "%FW%\bootloader.bin"
  exit /b 1
)
if not exist "%FW%\partition-table.bin" (
  echo Missing firmware file: "%FW%\partition-table.bin"
  exit /b 1
)
if not exist "%FW%\tab5_drummer.bin" (
  echo Missing firmware file: "%FW%\tab5_drummer.bin"
  exit /b 1
)

echo Flashing Aby55 to %PORT%...
"%PY%" -m esptool --chip esp32p4 -p %PORT% -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x2000 "%FW%\bootloader.bin" 0x8000 "%FW%\partition-table.bin" 0x10000 "%FW%\tab5_drummer.bin"
exit /b %ERRORLEVEL%
