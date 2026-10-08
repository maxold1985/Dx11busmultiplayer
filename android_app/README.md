# DX11BusAndroid — OpenGL ES 3.0, Android Studio e NDK

Este aplicativo **acrescenta** um cliente gráfico Android ao projeto original.
Os executáveis DirectX 11 do Windows (`src/client.cpp` e `src/server.cpp`)
não foram substituídos. O Android mantém o protocolo multiplayer BUS4
(porta UDP 27015) e compartilha os leitores de modelos e scripts OMSI.

## Gerar e instalar o APK

1. Abra a pasta `android_app/` no Android Studio.
2. Instale **JDK 17**, **Android SDK 35**, **NDK 27.0.12077973**
   e **CMake 3.22.1**.
3. Configure **Gradle 8.7**; se não existir o Gradle Wrapper, use a
   distribuição local do Gradle ou execute
   `gradle wrapper --gradle-version 8.7` nesta pasta.
4. Selecione **Build > Build APK(s)**, variante `debug`, ou execute
   `build_apk.bat` na pasta `android_app` com Gradle no PATH.
5. O arquivo de saída é `app/build/outputs/apk/debug/app-debug.apk`.
6. Instale o APK com o Android Studio ou execute `install_apk.bat`
   com o tablet conectado via ADB. Requer dispositivo **arm64-v8a**,
   API 26 ou superior.
   A visualização requer GPU com OpenGL ES 3.0.

Também existe o workflow
[Android APK](../.github/workflows/android-apk.yml) do GitHub Actions:
após uma execução bem-sucedida, baixe o artefato
`DX11BusAndroid-debug-arm64`. Esse é um caminho alternativo caso você
não tenha Gradle instalado no Windows.

O script `build_ndk.bat` **na raiz** continua produzindo as bibliotecas
e binários Android de terminal. **Não gera o APK gráfico.**

## Controles Android

- **PASTA OMSI**: escolha a pasta completa do ônibus usando o seletor do Android.
  O aplicativo copia os arquivos para armazenamento privado preservando
  as subpastas de malhas, scripts, sons e texturas.
- **ZIP OMSI**: escolha diretamente o ZIP do mod. O aplicativo extrai os
  arquivos dentro do armazenamento privado com limites de tamanho e
  verificação de caminhos para evitar *zip slip*.
- **MODELO** e **SCRIPT**: escolhem arquivo isolado. Para arquivos que
  referenciam outros, use a importação da pasta inteira.
- **MODELOS MOD** e **SCRIPTS MOD**: permitem escolher uma variante dentre
  os arquivos já copiados, sem perder os caminhos relativos do mod.
- **CONECTAR**: informe o IPv4 do servidor Windows ou Android na porta 27015.
  O endereço fica salvo para a próxima utilização.
- **CAMERA**: alterna câmera externa/cabine. Arraste na tela para
  orbitar, faça pinça para zoom ou pressione ZOOM + / ZOOM -.
- **VIDRO COR** e **VIDRO FACES**: diagnósticos de textura e culling;
  correspondem a F6 e F7 no Windows.
- **DESCONECTAR**: encerra o socket UDP e retorna à física offline.
- **ACELERAR, RE, ESQUERDA, DIREITA, FREIO, EMBREAGEM**:
  mantenha o botão pressionado.
- **PORTA, MARCHA +, MARCHA -, AUTO, DIRECAO OMSI, RESET**:
  enviam comandos pelo protocolo BUS4.
- **BUZINA, PARADA, SETA e FREIO MAO**:
  acionam os efeitos sonoros disponíveis nos scripts do ônibus.

Teclado físico: W/S acelerador/ré, A/D direção, Espaço freio,
Tab embreagem, E portas, Q/Z marchas, G automático, R reset,
H buzina, B pedido de parada, F1 câmera, F6/F7 diagnóstico de vidros.
Gamepad Bluetooth: direcional analógico esquerdo para dirigir,
gatilho esquerdo para frear, botão A portas e R1 aumentar marcha.

Sem servidor, o aplicativo executa uma **simulação local offline** com
a física compartilhada. Quando conectado, o servidor é autoritativo:
o cliente apenas envia controles e renderiza os snapshots recebidos.

## Compatibilidade de modelos

O build padrão, sem bibliotecas grandes, suporta:
- OMSI `.o3d`, `.3ds`, `model.cfg` e `.bus`, incluindo as malhas
  referenciadas por arquivos de configuração;
- Texturas PNG/JPG/BMP via BitmapFactory e DDS DXT1/DXT3/DXT5 e RGBA32;
- Profundidade, blending de vidros e animações básicas de portas/rodas.

### Habilitar importação Assimp (.x, .glb, .gltf, .fbx, .obj)

O importador Assimp é **opcional** e fica desativado por padrão.
Para habilitar, rode na pasta `android_app`:

```bat
gradle :app:assembleDebug -PbusAndroidAssimp=true
```

Isso ativa o download e a compilação do Assimp 5.4.3 pelo CMake.
Precisa de conexão à internet, utiliza mais RAM, leva mais tempo
e **ainda não foi validado em compilação Android real**.
Arquivos GLB com imagens PNG/JPEG incorporadas são extraídos
para a pasta privada do modelo. Importações mais complexas podem
exigir ajuste de materiais e animações.

## Cenário e áudio

O cenário procedural do Windows foi portado para OpenGL ES:
ruas, quadras, prédios, faixas e paradas com passageiros.
O raio de visibilidade móvel é reduzido para desempenho;
a visualização é limitada a cerca de 30 quadros por segundo.

O áudio Android utiliza **AAudio** (API 26+) com `stb_vorbis`:
- camadas OGG Vorbis e WAV PCM16 de motor com RPM/pitch/volume;
- sons nomeados de portas, buzina, freio e câmbio;
- síntese de motor diesel quando faltam arquivos de áudio.

A leitura de `Motor.txt` e `Cambio_A/M.txt` reutiliza
o parser de scripts OMSI, sem executar scripts `.osc`.
Em multiplayer, **configuração de transmissão/câmbio do cliente não
substitui a configuração do servidor**; configure o servidor
com `DX11BUS_MOD_CONFIG` para efetivar o câmbio personalizado.

## Estado de validação e diagnóstico

Arquivos e configurações estão implementados no repositório, mas
**não houve compilação, instalação ou teste de execução real neste ambiente**.
O funcionamento do APK, principalmente Assimp e áudio, precisa
ser confirmado no Android Studio/dispositivo.

Para erros de execução use:

```bat
adb logcat -s Dx11BusAndroid
```

Para erros de build, copie a primeira mensagem `error:` do Clang
ou a seção `FAILURE: Build failed` do Gradle.
