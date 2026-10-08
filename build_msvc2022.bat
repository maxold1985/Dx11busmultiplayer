@echo off
setlocal
cd /d "%~dp0"

where cmake.exe >nul 2>&1
if errorlevel 1 (
	echo ERRO: CMake nao encontrado. Instale o componente C++ CMake tools for Windows no Visual Studio 2022.
	exit /b 1
)

if not defined BUS_VS_PLATFORM set "BUS_VS_PLATFORM=x64"
if not defined BUS_WITH_ASSIMP set "BUS_WITH_ASSIMP=OFF"

echo Gerando Visual Studio 2022 para %BUS_VS_PLATFORM%...
cmake -S . -B "build_vs2022_%BUS_VS_PLATFORM%" -G "Visual Studio 17 2022" -A "%BUS_VS_PLATFORM%" -DBUS_WITH_ASSIMP=%BUS_WITH_ASSIMP%
if errorlevel 1 exit /b 1

echo Compilando configuracao Release...
cmake --build "build_vs2022_%BUS_VS_PLATFORM%" --config Release --parallel 2
if errorlevel 1 exit /b 1

ctest --test-dir "build_vs2022_%BUS_VS_PLATFORM%" -C Release --output-on-failure
if errorlevel 1 exit /b 1

echo.
echo Projeto: build_vs2022_%BUS_VS_PLATFORM%\DX11BusMultiplayer.sln
echo Cliente: build_vs2022_%BUS_VS_PLATFORM%\Release\bus_client.exe
echo Servidor: build_vs2022_%BUS_VS_PLATFORM%\Release\bus_server.exe
endlocal
