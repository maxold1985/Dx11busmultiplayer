@echo off
setlocal
cd /d "%~dp0"
set "APK=%CD%\app\build\outputs\apk\debug\app-debug.apk"

if not exist "%APK%" (
    echo ERRO: APK nao encontrado.
    echo Execute build_apk.bat antes.
    exit /b 1
)
where adb.exe >nul 2>&1
if errorlevel 1 (
    echo ERRO: adb nao encontrado no PATH.
    echo Abra uma janela de terminal com Android SDK platform-tools no PATH.
    exit /b 1
)
adb devices
echo.
echo Instalando DX11BusAndroid no dispositivo conectado...
adb install -r "%APK%"
if errorlevel 1 exit /b 1

echo.
echo Instalacao concluida. Abra DX11 Bus Android no tablet.
endlocal
