# DX11 Bus Multiplayer

Cliente Direct3D 11 e servidor UDP autoritativo para ate 16 onibus.

## WinLibs MinGW32

Instale WinLibs i686, CMake, mingw32-make e `fxc.exe` do Windows SDK. Execute `build_mingw32.bat`.

Servidor: `build_mingw32\bus_server.exe`.
Cliente: `build_mingw32\bus_client.exe 127.0.0.1`.
Rede local: substitua o IP pelo IPv4 do servidor. Porta UDP 27015.

Controles: W/S aceleracao e re, A/D direcao, Espaco freio, Esc sair.

Protótipo com fisica cinematica e shaders compilados antecipadamente; ainda sem suspensao raycast, colisao, interpolacao, autenticacao ou criptografia.

**Codigo nao validado por build real neste ambiente.**
