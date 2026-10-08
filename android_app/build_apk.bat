@echo off
setlocal
cd /d "%~dp0"

if not defined BUS_ANDROID_ASSIMP set "BUS_ANDROID_ASSIMP=false"
if not defined JAVA_HOME (
    echo ATENCAO: JAVA_HOME nao definido. Gradle 8.7 requer JDK 17.
)
if not defined ANDROID_HOME (
    if defined ANDROID_SDK_ROOT set "ANDROID_HOME=%ANDROID_SDK_ROOT%"
)

if exist gradlew.bat (
    echo Compilando APK usando Gradle Wrapper...
    call gradlew.bat --no-daemon :app:assembleDebug -PbusAndroidAssimp=%BUS_ANDROID_ASSIMP%
) else (
    where gradle.bat >nul 2>&1
    if errorlevel 1 (
        echo ERRO: Gradle nao encontrado.
        echo Abra android_app no Android Studio ou instale Gradle 8.7.
        echo Tambem pode usar GitHub Actions no repositorio.
        exit /b 1
    )
    echo Compilando APK usando Gradle instalado...
    call gradle.bat --no-daemon :app:assembleDebug -PbusAndroidAssimp=%BUS_ANDROID_ASSIMP%
)
if errorlevel 1 exit /b 1

echo.
if exist "app\build\outputs\apk\debug\app-debug.apk" (
    echo APK pronto: %CD%\app\build\outputs\apk\debug\app-debug.apk
) else (
    echo ERRO: Gradle terminou mas o APK nao apareceu.
    exit /b 1
)
endlocal
