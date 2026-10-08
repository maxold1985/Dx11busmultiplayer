@echo off
setlocal
cd /d "%~dp0"

if not defined ANDROID_NDK_HOME (
    if defined ANDROID_NDK_ROOT set "ANDROID_NDK_HOME=%ANDROID_NDK_ROOT%"
)
if not defined ANDROID_NDK_HOME (
    echo ERRO: defina ANDROID_NDK_HOME para a pasta do Android NDK.
    exit /b 1
)
if not exist "%ANDROID_NDK_HOME%\build\cmake\android.toolchain.cmake" (
    echo ERRO: android.toolchain.cmake nao encontrado.
    exit /b 1
)
where cmake.exe >nul 2>&1
if errorlevel 1 (
    echo ERRO: CMake nao encontrado.
    exit /b 1
)

if not defined BUS_ANDROID_ABI set "BUS_ANDROID_ABI=arm64-v8a"
if not defined BUS_ANDROID_API set "BUS_ANDROID_API=26"

cmake -S . -B "build_android_%BUS_ANDROID_ABI%" -G Ninja ^
    -DCMAKE_TOOLCHAIN_FILE="%ANDROID_NDK_HOME%/build/cmake/android.toolchain.cmake" ^
    -DANDROID_ABI=%BUS_ANDROID_ABI% ^
    -DANDROID_PLATFORM=android-%BUS_ANDROID_API% ^
    -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1

cmake --build "build_android_%BUS_ANDROID_ABI%" --parallel 2
if errorlevel 1 exit /b 1

echo.
echo Biblioteca gerada: build_android_%BUS_ANDROID_ABI%\libbus_android_core.so
echo Esta biblioteca contem simulacao/fisica, nao a interface DirectX.
endlocal
