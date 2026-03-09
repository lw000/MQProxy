@echo off
echo ========================================
echo Building MQProxy
echo ========================================

echo.
echo [1/2] Configuring CMake...
cmake -B build -DCMAKE_BUILD_TYPE=Debug
if %errorlevel% neq 0 (
    echo CMake configuration failed!
    pause
    exit /b 1
)

echo.
echo [2/2] Building project...
cmake --build build --config Debug
if %errorlevel% neq 0 (
    echo Build failed!
    pause
    exit /b 1
)

echo.
echo ========================================
echo Build successful!
echo ========================================
echo.
echo Run the application with:
echo   .\build\bin\MQProxy.exe
echo.
pause
