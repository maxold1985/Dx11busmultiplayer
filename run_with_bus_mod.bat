@echo off
setlocal EnableExtensions
rem Start DX11Bus server and client with one external bus INI script.
rem Windows 7 CMD compatible; quotes preserve spaces, accents and brackets.

if "%~1"=="" (
	echo Usage:
	echo   %~nx0 "F:\Mods\GV6\[SK8 Edits] Marcopolo Paradiso GV6 1150 MB O400RSD.ini"
	echo.
	echo Extract scripts.zip and supply the full path to its main .ini file.
	pause
	exit /b 1
)

set "DX11BUS_MOD_CONFIG=%~f1"
if not exist "%DX11BUS_MOD_CONFIG%" (
	echo ERROR: Bus INI not found:
	echo "%DX11BUS_MOD_CONFIG%"
	pause
	exit /b 2
)

set "BUS_DIR=%~dp0build_mingw32"
if not exist "%BUS_DIR%\bus_server.exe" (
    if exist "%~dp0build\bus_server.exe" (
        if exist "%~dp0build\bus_client.exe" (
            set "BUS_DIR=%~dp0build"
        )
    )
)

set "BUS_SERVER=%BUS_DIR%\bus_server.exe"
set "BUS_CLIENT=%BUS_DIR%\bus_client.exe"

if not exist "%BUS_SERVER%" (
	echo ERROR: Missing server executable:
	echo "%BUS_SERVER%"
	echo Compile first with build_mingw32.bat.
	pause
	exit /b 3
)

if not exist "%BUS_CLIENT%" (
	echo ERROR: Missing client executable:
	echo "%BUS_CLIENT%"
	echo Compile first with build_mingw32.bat.
	pause
	exit /b 4
)

echo Script:
echo "%DX11BUS_MOD_CONFIG%"
echo.
echo Starting server with automatic and manual gearbox configuration...
start "DX11BusServer" /D "%BUS_DIR%" "%BUS_SERVER%"

timeout /t 2 /nobreak >nul

echo Starting DX11 client with mod sound configuration...
start "DX11BusClient" /D "%BUS_DIR%" "%BUS_CLIENT%"

endlocal
