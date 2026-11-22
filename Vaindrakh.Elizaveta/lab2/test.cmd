@echo off
setlocal
cd /d "%~dp0"
wsl.exe -d Ubuntu-24.04 --cd "%~dp0." -- make test
pause
endlocal
