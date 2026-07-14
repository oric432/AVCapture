@echo off

setlocal

set "APP_NAME=AVCapture"
schtasks /end /tn "%APP_NAME%" >nul 2>&1
taskkill /f /im VSCApture.exe >nul 2>&1
schtasks /delete /tn "%APP_NAME%" /f >nul 2>&1
echo Removed "%APP_NAME%".

taskkill /f /im AVCaptureTray.exe >nul 2>&1
del /f /q "%APPDATA%\Microsoft\Windows Start Menu\Programs\Startup\AVCaptureTray.lnk" >nul 2>&1
echo Removed "AVCapture Tray".

pause
endlocal