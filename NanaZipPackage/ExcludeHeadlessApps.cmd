@echo off
rem ============================================================================
rem  ExcludeHeadlessApps.cmd  - TEMPORARY helper, do NOT commit
rem
rem  The Microsoft Store rejects the MSIX because it contains two headless
rem  Application entries with AppListEntry set to none:
rem    - NanaZip.Console  CLI: NanaZipC.exe, K7C.exe, 7z.exe
rem    - NanaZip.Windows  shell-launched G variant: NanaZipG.exe, K7G.exe, 7zG.exe
rem  Microsoft has not yet granted the HeadlessAppBypass waiver.
rem
rem  This script temporarily removes those two Application blocks from
rem  Package.appxmanifest so the package can be built and uploaded without the
rem  headless app. The main GUI Application stays, so the package stays valid.
rem
rem  Usage:
rem    ExcludeHeadlessApps.cmd         - back up manifest, then strip the 2 apps
rem    ExcludeHeadlessApps.cmd restore - restore the original manifest
rem
rem  After the default strip, build the MSIX as usual in Visual Studio or your
rem  normal build command, then run restore to put the manifest back.
rem  You can also simply run:  git checkout Package.appxmanifest
rem ============================================================================
setlocal EnableExtensions DisableDelayedExpansion

set "DIR=%~dp0"
set "MAN=%DIR%Package.appxmanifest"
set "BAK=%DIR%Package.appxmanifest.orig"
set "PS1=%DIR%StripHeadlessApps.ps1"

if /I "%~1"=="restore" goto :restore

rem ---- default: strip ----
findstr /L /C:"NanaZip.Console" "%MAN%" >nul
if errorlevel 1 (
  echo [!] Manifest already stripped. Nothing to do.
  echo     To rebuild after a restore, just run this script again.
  goto :eof
)

if not exist "%BAK%" (
  copy /Y "%MAN%" "%BAK%" >nul
  echo "[+] Backed up original manifest -> Package.appxmanifest.orig"
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1%" -Manifest "%MAN%"

rem verify the strip actually happened
findstr /L /C:"NanaZip.Console" "%MAN%" >nul
if not errorlevel 1 (
  echo "[!] Strip failed - NanaZip.Console still present. Restoring backup."
  copy /Y "%BAK%" "%MAN%" >nul
  goto :eof
)
echo "[+] Removed NanaZip.Console and NanaZip.Windows headless entries."
echo "[+] The main GUI Application remains, so the package is still valid."
echo "[+] Now build the MSIX normally, then run:  %~nx0 restore"
goto :eof

:restore
if exist "%BAK%" (
  copy /Y "%BAK%" "%MAN%" >nul
  del /Q "%BAK%" >nul 2>&1
  echo "[+] Restored Package.appxmanifest from backup."
) else (
  echo "[!] No backup found. Package.appxmanifest.orig is missing."
  echo "    If you only stripped and did not build, just: git checkout Package.appxmanifest"
)
goto :eof
