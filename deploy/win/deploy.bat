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
pause
endlocal