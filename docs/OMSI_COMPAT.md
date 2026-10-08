# Compatibilidade OMSI 2 / openOMSI (DX11BusMultiplayer)

## Escolher um modelo original

1. Tenha os arquivos do onibus OMSI 2 ou de um mod licenciado/instalado localmente. Este repositorio nao inclui modelos ou texturas do jogo.
2. Compile o cliente DX11 com WinLibs MinGW32 e `fxc.exe` (build_mingw32.bat).
3. Abra `bus_client.exe`. Clique **Carregar OMSI (.bus)** e escolha um arquivo, por exemplo:

   `D:\Jogos\Steam\steamapps\common\OMSI 2\Vehicles\MAN_NL_NG\MAN_NL262.bus`

   Esse caminho e ilustrativo: use um .bus que exista na sua instalacao. Tambem sao aceitos .ovh, model.cfg e arquivos .o3d ou .3ds individuais. Um .3ds com texturas exige os arquivos de imagem referenciados proximos ao modelo ou na pasta Texture do onibus.
4. Confira a mensagem com quantidade de malhas importadas. O cliente grava detalhes em `omsi_import.log` ao lado de `bus_client.exe`.
5. Clique **Conectar** e entre no servidor multiplayer. O mesmo modelo sera desenhado para todos os onibus deste cliente.

**Alternativa**: antes de executar, configure `DX11BUS_OMSI_BUS` para o caminho completo do arquivo .bus:

```bat
set "DX11BUS_OMSI_BUS=D:\OMSI 2\Vehicles\MeuOnibus\meuonibus.bus"
build_mingw32\bus_client.exe
```

## Arquivos suportados

| Arquivo | Implementacao |
|---|---|
| `.bus` / `.ovh` | Lê `[model]` e localiza o `.cfg` |
| `model.cfg` | Lê `[mesh]` em ordem, com caminho relativo |
| `.o3d` | Leitura binaria nativa das versoes 1, 3, 4, 5 e 7 (geometria, UV, normais, triangulos, materiais e matriz de pivot) |
| `.3ds` 3D Studio | Leitor binario nativo C++11 (objetos, vertices, faces, UV, texturas e materiais), sem Assimp |
| `.x` DirectX legado | Opcional com Assimp i686 (`BUS_WITH_ASSIMP=ON`) |
| `.bmp` `.png` `.jpg` | Textura via Windows Imaging Component |
| `.tga` | 24/32-bit true-color, sem paleta, cru ou RLE |
| `.dds` | DXT1, DXT3 e DXT5 (BC1/2/3), com DX10 BC1/2/3, somente mip 0 |

Busca por texturas em `Texture/` do diretorio do onibus, junto das malhas e junto ao diretorio de modelo. Arquivos externos permanecem na instalacao OMSI.

## Animacoes parciais

O parser reconhece `[newanim]`, `origin_trans`, `origin_rot_x/y/z`, `origin_from_mesh`, `anim_rot` e `anim_trans`. Uma ponte limitada mapeia variaveis **Wheel_Rotation_**, **Axle_Steering_0_**, **Axle_Suspension_**, **door_** e comandos de volante para estados ja simulados pelo jogo. Essas animacoes sao aproximadas, nao executam arquivos `.osc` do OMSI.

## Limites (nao e compatibilidade integral de OMSI/OpenOMSI)

- A posicao e fisica ainda sao as do DX11BusMultiplayer; nao le massa, suspension.cfg, colisoes OMSI ou transmissoes originais.
- Nao executa scripts OMSI `.osc`, plugin DLLs, HOF/IBIS, display dinamico, sons originais ou troca de pinturas `.cti`.
- `[matl]` especifico, alpha/reflexao por variavel, transmap e efeitos avancados nao sao reproduzidos fielmente; importacao usa materiais estaticos `.o3d`.
- `.o3d` protegidos ou com extensoes desconhecidas podem falhar; inspecte `omsi_import.log`.
- Geometria `.3ds` e `.o3d` nao precisa Assimp. Apenas `.x` exige Assimp. O importador 3DS usa as coordenadas dos vertices como exportadas; nao aplica transformacoes 0x4160 nem reproduz animacoes 3DS.
- Algumas portas/rodas podem ficar sem animacao; toda geometria estaticamente importada segue o bus.
- No multiplayer, cada cliente escolhe seu proprio modelo; servidor nao envia modelo/textura nem oferece sincronizacao OMSI completa.
- Testes de parser incluem um arquivo 3DS sintetico e validacao local com modelo GV1150.3ds do usuario (548 malhas, 690053 vertices, 827849 triangulos); o modelo comercial nao acompanha o repositorio. A importacao real e o build MinGW32 em Windows 7/10 ainda precisam ser verificados com arquivos legais do usuario.

Documentacao publica do formato: https://github.com/openOMSI-Project/openOMSI/blob/main/docs/FORMATS.md
Projeto openOMSI: https://github.com/openOMSI-Project/openOMSI
