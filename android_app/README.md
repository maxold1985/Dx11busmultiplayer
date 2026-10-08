# DX11BusAndroid (Android Studio + NDK)

Aplicativo gráfico Android baseado em OpenGL ES 3.0. É adicional:
nenhum arquivo do cliente ou servidor DirectX 11 do Windows foi substituído.

## Compilar

Abra a pasta **android_app** no Android Studio. Instale **Android SDK 35**,
**Android NDK**, **CMake 3.22.1** e use **JDK 17**.
Se o Android Studio solicitar um Gradle Wrapper ausente, execute
**gradle wrapper --gradle-version 8.7** na pasta android_app com Gradle
instalado, ou configure uma distribuição local do Gradle 8.7.
Execute **Build > Build APK(s)**. O projeto usa ABI arm64-v8a
(API 26 ou superior). Java Activity e biblioteca JNI são incluídos no APK.

A versão de console no diretório raiz (build_ndk.bat) continua disponível separadamente.

## Controles

- **MODELO**: escolha um arquivo .o3d, .3ds, model.cfg ou .bus.
- **SCRIPT**: escolha .ini ou Cambio_A/M.txt (parser de scripts, sem execução de .osc).
- **PASTA OMSI**: escolha a pasta completa do mod usando seletor de diretórios;
  o aplicativo copia os arquivos para o armazenamento privado, preservando a
  hierarquia de pastas relativa, incluindo malhas, configurações e texturas.
  A seleção automática privilegia .bus, model.cfg, .3ds e .o3d.
- **CONECTAR**: informe o IPv4 do servidor Windows/Android BUS4 na porta UDP 27015.
- **CAMERA**: alterna entre câmera externa e cabine.
- **RESET / PORTA / MARCHA / AUTO / DIRECAO OMSI**: enviam flags BUS4.
- **ACELERAR / RE / ESQUERDA / DIREITA / FREIO / EMBREAGEM**:
  mantenha os botões pressionados.
- Arraste sobre a imagem para orbitar a câmera.

## Estado da compatibilidade

Implementado: janela 3D real (GLSurfaceView + GLES 3.0), shader,
profundidade, alpha blending de vidros, DDS (DXT1/DXT3/DXT5 e RGBA32),
carregamento de .o3d e .3ds,
câmera, posição dos ônibus e veículos IA recebida por UDP,
texturas PNG/JPG/BMP decodificáveis pelo Android, seleção de arquivos
via Storage Access Framework e parser de scripts OMSI.

A versão Android reproduz o mapa procedural do cliente Windows com ruas,
prédios, paradas e passageiros, dentro de um raio menor para desempenho móvel.
Animações básicas de model.cfg (portas/direção) e rodas nomeadas em 3DS
foram portadas. Formatos .x, GLB/FBX, áudio e scriptagem executável .osc
ainda não são suportados no renderizador Android.
O script carregado no cliente é lido para consulta/configuração, mas a física
multiplayer continua autoritativa no servidor: para alterar câmbio,
importe o script também no servidor Windows, via DX11BUS_MOD_CONFIG.

Sem build/teste executado em Android Studio/NDK neste ambiente. Envie erros
de Gradle/CMake/Clang ou imagem da tela para aprimorarmos o port.
