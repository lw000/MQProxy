@echo off
echo ========================================
echo Building MQProxy (Release)
echo ========================================

echo.
echo [1/2] Configuring CMake...
cmake -B build -DCMAKE_BUILD_TYPE=Release
if %errorlevel% neq 0 (
    echo CMake configuration failed!
    pause
    exit /b 1
)

echo.
echo [2/2] Building project...
cmake --build build --config Release
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
echo   .\build\bin\Release\MQProxy.exe
echo.
pause
