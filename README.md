# DX11BusMultiplayer

Projeto C++11 de simulador de onibus multiplayer em DirectX 11, com servidor UDP autoritativo, fisica e interface Win32. Repositorio em desenvolvimento; build DX11 no Windows ainda nao validado neste ambiente.

## Recursos

- 16 jogadores humanos e 4 veiculos IA no trafego.
- Simulacao 60 Hz, snapshots UDP 20 Hz, interpolacao visual de estados remotos.
- Suspensao raycast 6 rodas: molas/amortecedores calibrados para 12 t, distribuicao de carga 43/24/33% entre eixos, barra estabilizadora e curso visual no 3DS. Tracao/freios e aderencia consideram a carga de contato das rodas.
- Colisao OBB/SAT entre onibus e com edificios da cidade procedural.
- Direcao com Ackermann, esterco progressivo, assistencia em velocidade e resposta de guinada limitada pela aderencia lateral.
- Modo de direcao OMSI 2 aproximado (tecla **Ç** no teclado ABNT2 ou botao na janela), alternavel por jogador, com mais esterco em baixa velocidade, retorno progressivo do volante e resposta de curvas recalibrada; servidor autoritativo.
- Cidade procedural ampliada para 4,08 km x 4,08 km, com ruas a cada 60 m, predios renderizados apenas perto do onibus e 28 paradas de passageiros.
- Reset de posicao na origem (0, 0) pela tecla R ou botao, executado no servidor sem desconectar o jogador; indicador de coordenadas X/Z na janela.
- Porta controlavel, embarque por aproximacao e parada, lotacao 40, indicadores de RPM, marcha, velocidade e rota.
- Motor sintetico via WinMM como fallback e leitor de arquivos de som WAV/OGG Vorbis (Motor.txt [soundN]) com pitch e volume por RPM, camera externa/interna e texturas WIC.
- Leitor dos arquivos INI de Cambio_A.txt e Cambio_M.txt: motor, 6 marchas, relacoes, diferencial, trocas por RPM, pausa de engate, mudanca Q/Z e retorno ao automatico G. Perfis aplicados no servidor via DX11BUS_MOD_CONFIG.
- Importador opcional Assimp para GLB/FBX com texturas externas e embutidas.

## Compilar com WinLibs MinGW32 i686

Requer Windows, WinLibs MinGW32, CMake, Windows SDK com Direct3D 11 headers/libs e fxc.exe (shader compiler HLSL).

1. Ajuste MINGW em build_mingw32.bat conforme a sua pasta.
2. Defina FXC_EXECUTABLE para o caminho de fxc.exe ou adicione ao PATH.
3. Execute build_mingw32.bat e confira quaisquer erros.

Para importar modelos GLB/FBX instale Assimp compilado para i686 com ABI compativel com o mesmo WinLibs. Configure manualmente CMake com BUS_WITH_ASSIMP=ON e assimp_DIR apontando ao pacote CMake dessa biblioteca. O build .bat usa OFF para nao depender de Assimp.

## Executar

Execute primeiro build_mingw32/bus_server.exe, depois build_mingw32/bus_client.exe. Na janela digite o IPv4 do servidor, como 127.0.0.1. Para outros computadores, use o IPv4 da rede local, liberando porta UDP 27015 no firewall. Pode executar bus_client.exe 192.168.1.10 para conectar diretamente.

Detalhes: [suspensao e direcao](docs/SUSPENSION_STEERING.md), [mapa ampliado e reset de origem](docs/MAP_ORIGIN.md) e [scripts INI, cambio e sons WAV/OGG](docs/SCRIPTS_AUDIO.md).

Codigo C++11 tabulado, com convencoes de formatacao em [.clang-format](.clang-format).

## Controles

| Tecla | Comando |
|---|---|
| W / S | Acelerar / re |
| A / D | Direcao |
| Espaco | Frear |
| E | Porta (com onibus parado) |
| Q / Z | Marcha manual acima / abaixo |
| G | Retomar cambio automatico |
| R | Resetar onibus para origem (X=0, Z=0) |
| Ç (ABNT2) | Alternar modo de direcao OMSI 2 aproximado / classico |
| Botao Direcao OMSI: ON/OFF (Ç) | Alternativa a tecla Ç; exibe estado confirmado pelo servidor |
| Botao Ler Scripts/Sons | Abre arquivo INI ou TXT do mod para leitura dos arquivos de audio. A transmissao e configurada no servidor. |
| Botao Reset origem (R) | Alternativa ao teclado |
| F1 | Camera interna / externa |
| Botao direito do mouse + arrastar | Orbitar camera externa |
| Roda do mouse | Zoom da camera externa |
| Esc | Sair |

## Configurar cambio e som de outro onibus

O arquivo `scripts.zip` analisado e um pacote de configuracoes INI,
**nao** e um script `.osc` original do OMSI 2. Extrair o ZIP em uma
pasta local e configurar o arquivo principal `.ini` para o servidor:

```bat
set "DX11BUS_MOD_CONFIG=F:\\Mods\\GV6\\[SK8 Edits] Marcopolo Paradiso GV6 1150 MB O400RSD.ini"
build_mingw32\\bus_server.exe
```

Use o mesmo caminho no cliente, ou clique em **Ler Scripts/Sons**.
No Windows 7, voce tambem pode iniciar ambos diretamente com:

```bat
run_with_bus_mod.bat "F:\\Mods\\GV6\\[SK8 Edits] Marcopolo Paradiso GV6 1150 MB O400RSD.ini"
```

Sem a pasta de arquivos OGG/WAV originais, o audio continua sintetico.
Consulte [docs/SCRIPTS_AUDIO.md](docs/SCRIPTS_AUDIO.md).

## Carregamento nativo de modelos OMSI 2 / openOMSI

O cliente DX11 aceita modelos proprios de OMSI 2 por meio de `.bus` -> `model.cfg` -> `.o3d` (sem Assimp). Abra `bus_client.exe` e use o botao **Carregar OMSI (.bus/.3ds)**; escolha o .bus dentro da instalacao OMSI 2. Tambem aceita diretamente `.cfg`, `.o3d` e `.3ds`. Texturas `.bmp`, `.png`, `.jpg`, `.tga` e `.dds` (BC1/2/3) sao carregadas do diretorio do onibus. `.3ds` e carregado nativamente (sem Assimp). Apenas `.x` requer Assimp i686 opcional (`BUS_WITH_ASSIMP=ON`). O arquivo `omsi_import.log` lista malhas nao carregadas.

O mesmo modelo carregado neste cliente representa todos os onibus visiveis por ele. No `.3ds`, rodas nomeadas `wheel_fl/fr/rl2/rr2/rl/rr` giram conforme o deslocamento e as dianteiras estercam com a direcao. Animacoes de rodas, suspensao e portas estao mapeadas parcialmente; scripts, sistema HOF/IBIS, CTI e logica completa de OMSI ainda nao sao executados. Detalhes em [docs/OMSI_COMPAT.md](docs/OMSI_COMPAT.md). Nenhum conteudo original de OMSI acompanha este projeto.

## Texturas / modelo

O jogo cria um onibus procedural com textura gerada internamente. Opcionalmente coloque assets/bus.png no mesmo diretorio do executavel. Para modelo GLB/FBX, habilite Assimp e coloque assets/bus.glb ou assets/bus.fbx ao lado do executavel, mantendo texturas referenciadas. O modelo precisa ser exportado na escala e origem certas. Modelos externos nao acompanham o repositorio.

## Testes de fisica sem Windows

Em Linux ou ambiente C++11: cmake -S . -B build_native && cmake --build build_native && ctest --test-dir build_native --output-on-failure

## Limites conhecidos

Este e um prototipo: colisao 2D simplificada, suspensao em terreno por altura, IA de trafego por pontos fixos, contador de passageiros sem pedestres animados, e nenhuma predicao de cliente, NAT traversal, autenticacao, criptografia ou anti-cheat. Nao afirmar compatibilidade WinLibs antes da compilacao real.
