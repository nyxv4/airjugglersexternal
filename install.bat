@echo off
setlocal
set "DIR=%LOCALAPPDATA%\Programs\NYX"
mkdir "%DIR%" 2>nul

echo Downloading NYX...
curl -L --fail -o "%DIR%\NyxExternal.exe" "https://github.com/nyxv4/airjugglersexternal/raw/main/releases/NyxExternal.exe"
if errorlevel 1 (
    echo Download failed. Check your internet connection and try again.
    pause
    exit /b 1
)

powershell -NoProfile -Command "$ws = New-Object -ComObject WScript.Shell; $desktop = $ws.CreateShortcut([IO.Path]::Combine($env:USERPROFILE, 'Desktop', 'NYX.lnk')); $desktop.TargetPath = '%DIR%\NyxExternal.exe'; $desktop.WorkingDirectory = '%DIR%'; $desktop.Save(); $start = $ws.CreateShortcut([IO.Path]::Combine($env:APPDATA, 'Microsoft\Windows\Start Menu\Programs', 'NYX.lnk')); $start.TargetPath = '%DIR%\NyxExternal.exe'; $start.WorkingDirectory = '%DIR%'; $start.Save()"

echo.
echo NYX installed to %DIR%
echo Desktop and Start Menu shortcuts created.
echo It will update itself automatically from now on.
pause
