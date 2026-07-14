@echo off
setlocal
set "TASK=AVCapture"
set "SRC=%~dp0"
set "EXE=%SRC%AVCapture.exe"
set "XML=%TEMP%"
set "USERID=%USERDOMAIN%\%USERNAME%"

if not exist "%EXE%" (
    echo AVCapture executable not found in the current folder. Please include vscapture.exe.
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -Command "Unblock-File -Path '%~dp0deploy_task.ps1'; & '%~dp0deploy_task.ps1' -TaskName '%TASK%' -ExePath '%EXE%' -UserId '%USERID%'"

echo Installed "%TASK%" for user "%USERID%".

set "TRAY_EXE=%SRC%AVCaptureTray.exe"
if exist "%TRAY_EXE%" (
    powershell -NoProfile -ExecutionPolicy Bypass -Command "$startup = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Startup'; $shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut((Join-Path $startup 'AVCaptureTray.lnk')); $shortcut.TargetPath = '%TRAY_EXE%'; $shortcut.Save(); Start-Process -FilePath '%TRAY_EXE%'"
    echo Installed AVCapture Tray startup shortcut and launched it.
) else (
    echo AVCaptureTray.exe not found in the current folder. Skipping tray autostart.
)

pause
endlocal