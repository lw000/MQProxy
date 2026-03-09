@echo off
setlocal enabledelayedexpansion

echo ========================================
echo Backup MQProxy Project
echo ========================================

set "SOURCE_DIR=d:\work\cpp_work\MQProxy"
set "BACKUP_DIR=d:\backup_kafka"

echo.
echo [1/3] Cleaning old backup...
if exist "%BACKUP_DIR%" (
    rmdir /S /Q "%BACKUP_DIR%"
)
mkdir "%BACKUP_DIR%"

echo.
echo [2/3] Copying files (excluding build artifacts)...
robocopy "%SOURCE_DIR%" "%BACKUP_DIR%" /E ^
    /XF "*.obj" "*.o" "*.lib" "*.pdb" "*.tlog" "*.recipe" ^
         "*.cmake" "*.txt" "*.vcxproj" "*.vcxproj.filters" ^
         "*.sln" "CMakeCache.txt" "cmake_install.cmake" ^
    /XD "build" "logs"

echo.
echo [3/3] Renaming .cpp and .h files to add .txt suffix...
for /r "%BACKUP_DIR%\include" %%f in (*.h) do (
    ren "%%f" "%%~nf.h.txt"
)
for /r "%BACKUP_DIR%\src" %%f in (*.cpp) do (
    ren "%%f" "%%~nf.cpp.txt"
)

echo.
echo ========================================
echo Backup completed!
echo Backup location: %BACKUP_DIR%
echo ========================================
echo.
pause
