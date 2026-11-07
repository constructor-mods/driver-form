@echo off
rem Double-click: builds the DLL and makes both release files in release\.
cd /d "%~dp0"
"%ProgramFiles%\Git\bin\bash.exe" native/build.sh || goto :fail
powershell -NoProfile -ExecutionPolicy Bypass -File tools\package.ps1 || goto :fail
echo.
echo Done. The files are in the release folder.
pause
exit /b 0
:fail
echo.
echo Something failed, see above.
pause
exit /b 1
