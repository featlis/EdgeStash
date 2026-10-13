@echo off
setlocal

echo ===================================================
echo [EdgeStash] Building Windows Installer
echo ===================================================

echo [1/2] Checking EdgeStash.exe...
if not exist "EdgeStash.exe" (
    echo [INFO] EdgeStash.exe not found. Building first...
    call build.bat
    if %errorlevel% neq 0 (
        echo [ERROR] Application build failed!
        exit /b 1
    )
)

echo [2/2] Compiling Installer with Inno Setup 6...
set "ISCC_PATH=C:\Users\aggsh\AppData\Local\Programs\Inno Setup 6\iscc.exe"

if not exist "%ISCC_PATH%" (
    where iscc.exe >nul 2>&1
    if %errorlevel% equ 0 (
        set "ISCC_PATH=iscc.exe"
    ) else (
        echo [ERROR] Inno Setup 6 iscc.exe was not found!
        exit /b 1
    )
)

if not exist "dist" mkdir dist

"%ISCC_PATH%" installer\edgestash_setup.iss
if %errorlevel% neq 0 (
    echo [ERROR] Inno Setup compilation failed!
    exit /b 1
)

echo.
echo ===================================================
echo [SUCCESS] Installer created successfully!
echo ===================================================
dir dist\EdgeStash_Setup_v2.0.0.exe
