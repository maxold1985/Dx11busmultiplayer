@echo off
setlocal
cd /d "%~dp0"
rem WinLibs i686: altere se sua instalacao estiver em outra pasta.
set "MINGW=F:\mingw64\mingw32\bin"
set "PATH=%MINGW%;%PATH%"
where g++.exe >nul 2>&1 || (echo ERRO: g++.exe nao encontrado em %MINGW% & exit /b 1)
where mingw32-make.exe >nul 2>&1 || (echo ERRO: mingw32-make.exe nao encontrado & exit /b 1)
where cmake.exe >nul 2>&1 || (echo ERRO: cmake.exe nao encontrado & exit /b 1)
if "%FXC_EXECUTABLE%"=="" (
    where fxc.exe >nul 2>&1 || (echo ERRO: defina FXC_EXECUTABLE com caminho completo de fxc.exe & exit /b 1)
    set "FXC_EXECUTABLE=fxc.exe"
)
cmake -S . -B build_mingw32 -G "MinGW Makefiles" -DCMAKE_C_COMPILER="%MINGW%\gcc.exe" -DCMAKE_CXX_COMPILER="%MINGW%\g++.exe" -DFXC_EXECUTABLE="%FXC_EXECUTABLE%"
if errorlevel 1 exit /b 1
cmake --build build_mingw32 --parallel 2
if errorlevel 1 exit /b 1
echo.
echo Compilado: build_mingw32\bus_server.exe e bus_client.exe
endlocal
