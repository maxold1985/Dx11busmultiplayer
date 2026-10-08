@echo off
setlocal
cd /d "%~dp0"
set "MINGW=F:\mingw64\mingw32\bin"
set "PATH=%MINGW%;%PATH%"
where g++.exe >nul 2>&1 || (echo ERRO: g++.exe nao encontrado & exit /b 1)
where mingw32-make.exe >nul 2>&1 || (echo ERRO: mingw32-make.exe nao encontrado & exit /b 1)
where cmake.exe >nul 2>&1 || (echo ERRO: cmake.exe nao encontrado & exit /b 1)
if not defined BUS_WITH_ASSIMP set "BUS_WITH_ASSIMP=OFF"
if not defined FXC_EXECUTABLE (
    where fxc.exe >nul 2>&1 || (echo ERRO: defina FXC_EXECUTABLE com o caminho de fxc.exe & exit /b 1)
    set "FXC_EXECUTABLE=fxc.exe"
)
rem Use forward slashes for compiler paths in CMake-generated .cmake files.
rem CMake 3.27 with MinGW32 on Windows 7 may otherwise emit invalid \\m escapes.
set "MINGW_CMAKE=F:/mingw64/mingw32/bin"
if exist "build_mingw32\\CMakeCache.txt" (
    findstr /C:"CMAKE_C_COMPILER:FILEPATH=" "build_mingw32\\CMakeCache.txt" >nul 2>&1
    if not errorlevel 1 (
        echo Existing compiler cache detected; using clean configure for reliable C/CXX paths.
        rmdir /s /q "build_mingw32"
    )
)
cmake -S . -B build_mingw32 -G "MinGW Makefiles" -DCMAKE_C_COMPILER=%MINGW_CMAKE%/gcc.exe -DCMAKE_CXX_COMPILER=%MINGW_CMAKE%/g++.exe -DFXC_EXECUTABLE="%FXC_EXECUTABLE%" -DBUS_WITH_ASSIMP=%BUS_WITH_ASSIMP%
if errorlevel 1 exit /b 1
cmake --build build_mingw32 --parallel 2
if errorlevel 1 exit /b 1
ctest --test-dir build_mingw32 --output-on-failure
if errorlevel 1 exit /b 1
echo.
echo Servidor: build_mingw32\bus_server.exe
echo Cliente: build_mingw32\bus_client.exe
endlocal
