@echo off
setlocal
cd /d "%~dp0"
if not exist "host_mmap" goto build
if not exist "host_shm" goto build
if not exist "host_pipe" goto build
goto select
:build
wsl.exe -d Ubuntu-24.04 --cd "%~dp0." -- make
if errorlevel 1 (
    pause
    exit /b 1
)
:select
echo Variant 1 - Wolf and goat
echo 1. Anonymous mmap
echo 2. POSIX shm_open + mmap
echo 3. Anonymous pipe
choice /c 123 /n /m "Select transport [1-3]: "
if errorlevel 3 goto pipe
if errorlevel 2 goto shm
:mmap
wsl.exe -d Ubuntu-24.04 --cd "%~dp0." -- ./host_mmap
goto done
:shm
wsl.exe -d Ubuntu-24.04 --cd "%~dp0." -- ./host_shm
goto done
:pipe
wsl.exe -d Ubuntu-24.04 --cd "%~dp0." -- ./host_pipe
:done
if errorlevel 1 pause
endlocal
