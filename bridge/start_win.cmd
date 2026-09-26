@echo off
setlocal EnableExtensions

rem Local disk only. For UNC path (\\server\share\...), double-click start_win.vbs instead.
pushd "%~dp0" 2>nul
if errorlevel 1 (
    echo [ERROR] Cannot enter script directory: %~dp0
    echo For network share path, double-click start_win.vbs instead of .cmd
    pause
    exit /b 1
)

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0start_win.ps1"
set "ERR=%ERRORLEVEL%"

popd
endlocal & exit /b %ERR%

