# Importacao de scripts INI, transmissao e audio de onibus

## Tipo de arquivos recebido

O arquivo `scripts.zip` do usuario contem configuracoes INI com secoes
`[engine]`, `[manual_gearbox]`, `[automatic_gearbox]`,
`[differential]`, `[soundN]` e um arquivo principal `*.ini`.

**Este formato nao e o script nativo `.osc` do OMSI 2.**
O leitor implementado importa os parametros conhecidos, sem executar codigo
desconhecido ou emular todo o motor de scripts OMSI.

## Como organizar arquivos do mod

Extraia os arquivos do ZIP em uma pasta deste tipo:

```text
GV6/
    [SK8 Edits] Marcopolo Paradiso GV6 1150 MB O400RSD.ini
    Script/
        Cambio_A.txt
        Cambio_M.txt
        Motor.txt
        Rodas.txt
        ...
    Sound/
        O-400/
            Mercedes Benz O400/
                idle.ogg
                1i.ogg
                x2i.ogg
                3i.ogg
                ...
```

Os arquivos `.txt` podem ficar na mesma pasta do `.ini`: o leitor tenta
encontrar o nome final quando o caminho especifica `Script/`.

O ZIP fornecido **nao inclui arquivos `.ogg` nem `.wav`**. Para ouvir
os sons originais, copie tambem a pasta de audio correspondente ao mod.
O leitor aceita WAV PCM (8/16 bits, mono/estereo) e OGG Vorbis.
O decoder OGG integrado e `third_party/stb_vorbis.c`, publico/dominio publico.

## Configuracao da transmissao no servidor

O servidor e responsavel pela fisica, e portanto deve abrir o mesmo
arquivo INI do mod que o cliente.

Execute os dois programas em uma janela `cmd.exe` com a variavel de ambiente
configurada:

```bat
set "DX11BUS_MOD_CONFIG=F:\Mods\GV6\[SK8 Edits] Marcopolo Paradiso GV6 1150 MB O400RSD.ini"
build_mingw32\bus_server.exe
```

Abra outro `cmd` com o mesmo `set` e execute:

```bat
set "DX11BUS_MOD_CONFIG=F:\Mods\GV6\[SK8 Edits] Marcopolo Paradiso GV6 1150 MB O400RSD.ini"
build_mingw32\bus_client.exe
```

Se preferir, inicie o cliente e clique em **Ler Scripts/Sons** para selecionar
o `.ini`; nesse caso, o cliente carregara apenas a configuracao de audio.
O servidor ainda precisa da variavel `DX11BUS_MOD_CONFIG` ao iniciar
para configurar as marchas. O arquivo `bus_scripts.log` no diretorio
do executavel registra o resultado da leitura.

## Controles

- `W`: acelerar em marcha a frente.
- `S`: pedir marcha a re simplificada.
- `Q`: aumentar marcha e selecionar modo manual.
- `Z`: reduzir marcha e selecionar modo manual.
- `G`: voltar ao modo automatico se a configuracao automatica estiver disponivel.
- `Ç`: alternar entre direcao classica e direcao OMSI aproximada.
- `R`: redefinir posicao na origem sem perder a configuracao do cambio.

O titulo da janela mostra a marcha atual e o modo de transmissao
`Manual` ou `Automatica` confirmado pelo servidor.

## Parametros efetivamente implementados

`Cambio_A.txt` e `Cambio_M.txt`:

- Rotacao de marcha lenta, rotacao de torque maximo e limite de RPM.
- Torque na marcha lenta e no pico do motor, com aproximacao da curva.
- Numero de marchas (ate 6), relacoes de 1 a 6, re e diferencial.
- `gear_up_rpm`, `gear_down_rpm`, `next_gear_min_speed`.
- `gear_change_time` como pausa simplificada de tracao durante engate.
- `mass1` do descritor para calculo aproximado de aceleracao.
- Trocas feitas por `sim::step()` no servidor com passo de 60 Hz.
- Modo de cambio replicado no `BusState`, protocolo **BUS4**.

Os scripts enviados contem, para o automatico, `gear_up_rpm=2170`,
`gear_down_rpm=900`, marcha lenta de 550 RPM e seis marchas.
O manual possui marcha lenta de 640 RPM e tempo de engate de 0,5 s.
Esses parametros sao carregados dos arquivos, nao codificados no cliente.

O simulador ainda nao implementa embreagem fisica em H, conversor de torque
de alta fidelidade, retarder dinamico completo, stalling, nem todas as
interacoes entre pneus, motor e cambio presentes no software original.

## Leitura e reproducao de sons

O leitor reconhece as secoes de som `[soundN]`, os campos `file`,
`whenToPlay`, `volumeMultiplier`, `pitchMultiplier`, `v1x/v1y` etc.,
`p1x/p1y`, `playOnlyWhenAccelerating`, `playOnlyOnSelectedGears`,
`minGearToPlay` e `maxGearToPlay`.

O cliente decodifica ate 12 camadas **de motor e transmissao** entre os
primeiros 18 sons numerados (com limite de 20 s por clipe carregado).
A reproducao e mixada via **WinMM waveOut PCM mono** com volume, pitch
dependente do RPM e selecao para camera interna/externa.

O manifesto `Motor.txt` referencia muitos sons de efeito, como portas,
botoes, buzinas e outros eventos. Seus caminhos sao lidos, mas os
eventos de reproducao correspondentes **ainda nao foram implementados**.
Arquivos de som ausentes ou invalidos nao interrompem o jogo.
Se nenhum som do mod carregar, o sistema sintetico de motor permanece ativo.

## Compilacao e testes

```bat
git pull origin mainn
set "BUS_WITH_ASSIMP=OFF"
build_mingw32.bat
ctest --test-dir build_mingw32 --output-on-failure
```

O novo alvo `bus_scripts_tests` verifica leitura INI, seis relacoes,
RPM e tempos de troca, curva de audio, medidas de seguranca para caminhos,
troca automatica, controles manuais e reset da configuracao.

O CMake passa a compilar C e C++11 porque `stb_vorbis.c` e escrito em C.
Reinicie **servidor e clientes** ao trocar de versao do programa:
a nova estrutura de pacote usa `BUS_MAGIC=BUS4`.
