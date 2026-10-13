@echo off
setlocal enabledelayedexpansion

echo ===================================================
echo [EdgeStash] Building Native Windows Widget (x64)
echo ===================================================

where cl.exe >nul 2>&1
if %errorlevel% neq 0 (
    echo [INFO] Searching for Visual Studio vcvars64.bat...
    set "VS_PATH="
    for %%p in (
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    ) do (
        if exist "%%~p" (
            set "VS_PATH=%%~p"
            goto :FoundVS
        )
    )
    echo [ERROR] Visual Studio 64-bit compiler tools not found!
    exit /b 1

:FoundVS
    echo [INFO] Initializing MSVC environment from !VS_PATH!...
    call "!VS_PATH!"
)

if not exist "build" mkdir build

echo [1/4] Compiling Windows Resources...
rc /nologo /fo build\resource.res src\resource.rc
if %errorlevel% neq 0 (
    echo [ERROR] Resource compilation failed!
    exit /b 1
)

echo [2/4] Compiling SQLite amalgamation...
if not exist "build\sqlite3.obj" (
    cl /nologo /O2 /MD /DSQLITE_THREADSAFE=1 /DSQLITE_OMIT_LOAD_EXTENSION /c src\sqlite3.c /Fo:build\sqlite3.obj
    if %errorlevel% neq 0 (
        echo [ERROR] SQLite compilation failed!
        exit /b 1
    )
) else (
    echo [2/4] SQLite obj up to date.
)

echo [3/4] Compiling GDI+ Renderer (C++)...
cl /nologo /O2 /MD /EHsc /std:c++17 /DUNICODE /D_UNICODE /Isrc /c src\renderer.cpp /Fo:build\renderer.obj
if %errorlevel% neq 0 (
    echo [ERROR] Renderer compilation failed!
    exit /b 1
)

echo [4/4] Compiling C modules and linking EdgeStash.exe...
cl /nologo /O2 /MD /W3 /std:c11 /DUNICODE /D_UNICODE /Isrc ^
   src\main.c src\window.c src\storage.c src\ttl_daemon.c src\drop_target.c src\drop_source.c src\tray.c ^
   build\renderer.obj build\sqlite3.obj build\resource.res ^
   /Fe:EdgeStash.exe ^
   /link /SUBSYSTEM:WINDOWS ^
   user32.lib gdi32.lib gdiplus.lib shell32.lib ole32.lib oleaut32.lib uuid.lib dwmapi.lib shlwapi.lib advapi32.lib

if %errorlevel% neq 0 (
    echo [ERROR] Link failed!
    exit /b 1
)

echo.
echo ===================================================
echo [SUCCESS] EdgeStash.exe built successfully!
echo ===================================================
dir EdgeStash.exe
