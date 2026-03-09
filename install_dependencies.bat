@echo off
echo ========================================
echo Installing vcpkg dependencies...
echo ========================================

set VCPKG_ROOT=D:\work\vcpkg
set TRIPLET=x64-windows

if not exist "%VCPKG_ROOT%" (
    echo vcpkg not found at %VCPKG_ROOT%
    echo Please install vcpkg first by running:
    echo   git clone https://github.com/Microsoft/vcpkg.git %VCPKG_ROOT%
    echo   cd %VCPKG_ROOT%
    echo   .\bootstrap-vcpkg.bat
    pause
    exit /b 1
)

echo.
echo Installing dependencies...
echo.

cd /d "%VCPKG_ROOT%"

echo [1/3] Installing rdkafka...
.\vcpkg install rdkafka:%TRIPLET%
if %errorlevel% neq 0 (
    echo Failed to install rdkafka
    pause
    exit /b 1
)

echo [2/3] Installing spdlog...
.\vcpkg install spdlog:%TRIPLET%
if %errorlevel% neq 0 (
    echo Failed to install spdlog
    pause
    exit /b 1
)

echo [3/3] Installing nlohmann-json...
.\vcpkg install nlohmann-json:%TRIPLET%
if %errorlevel% neq 0 (
    echo Failed to install nlohmann-json
    pause
    exit /b 1
)

echo.
echo Note: tomlplusplus is using local version in 3d-partys directory

echo.
echo ========================================
echo All dependencies installed successfully!
echo ========================================
echo.
echo Next steps:
echo   1. cd D:\work\cpp_work\MQProxy
echo   2. cmake -B build -DCMAKE_BUILD_TYPE=Debug
echo   3. cmake --build build --config Debug
echo.
pause
