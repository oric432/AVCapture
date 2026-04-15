@echo off

setlocal

set "APP_NAME=VSCapture"
schtasks /end /tn "%APP_NAME%" >nul 2>&1
taskkill /f /im VSCApture.exe >nul 2>&1
schtasks /delete /tn "%APP_NAME%" /f >nul 2>&1
echo Removed "%APP_NAME%".

pause
endlocal