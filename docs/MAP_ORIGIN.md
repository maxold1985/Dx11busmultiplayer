# Cidade ampliada e reset de origem

## Extensao do mapa

O mapa procedural usa coordenadas X/Z de **-2040 a +2040 metros**.
Isso corresponde a uma area de **4080 x 4080 metros (4,08 km por lado)**.

- Ruas a cada 60 metros, em ambos os eixos.
- Predios gerados deterministicamente por quarteirao.
- O cliente renderiza apenas ruas e predios perto da posicao do onibus.
- `MAP_RENDER_RADIUS` (em `src/simulation.hpp`) define a distancia
  em quarteiroes exibida por lado, evitando desenhar milhares de predios.
- Existem 28 pontos de parada. Os seis originais continuam presentes.
- O terreno e o sistema de colisao utilizam os mesmos limites do mapa.
- O simulador continua com terreno de altura procedural, sem colisao
  fisica raycast com malhas arbitrarias importadas.

## Reset de origem (0, 0)

Pressione **R** ou clique em **Reset origem (R)** no cliente conectado.

O cliente envia `INPUT_RESET_ORIGIN` via UDP, por 300 milissegundos.
O servidor processa o primeiro pacote com a flag e ignora repeticoes
ate o comando ser liberado.

O reset executado em `sim::resetOrigin` redefine:

- Posicao `X=0`, `Z=0`; altura restaurada para o repouso da suspensao.
- Heading, velocidade, esterco, rolagem/arfagem e velocidades fisicas zerados.
- Marcha padrao, embarque e passageiros restaurados ao estado inicial.
- O ID do jogador **e preservado**, sem perder a conexao multiplayer.
- A camera orbital volta ao angulo e distancia padrao.
- Para um deslocamento grande, a interpolacao do cliente salta direto
  para o novo local, sem arrastar o onibus pela tela.

O centro do mapa tem uma pequena cruz no chao (X vermelho e Z azul)
para identificar a origem.

## Arquivos

- `src/simulation.hpp`: limites do mapa, predios, paradas e reset.
- `src/protocol.h`: flag `INPUT_RESET_ORIGIN`.
- `src/server.cpp`: simula e valida reset no servidor.
- `src/client.cpp`: renderizacao procedural em area proxima, tecla R,
  botao de reset, coordenadas no titulo da janela.
- `tests/map_origin_test.cpp`: regressao de limites, ruas, paradas e reset.
- `.clang-format`: padrao de codigo C++11 com TAB e uma instrucao por linha.

## Compilar (WinLibs MinGW32)

```bat
git pull origin mainn
set "BUS_WITH_ASSIMP=OFF"
build_mingw32.bat
```

Feche o cliente e o servidor em execucao antes do build para evitar
`ld.exe: reopening bus_client.exe: Permission denied`.
Inicie **primeiro** o `bus_server.exe` atualizado; so depois conecte
o `bus_client.exe` atualizado.

Os testes portateis podem rodar com:

```bat
ctest --test-dir build_mingw32 --output-on-failure
```

O cliente DX11 ainda depende de validacao no Windows 7 / WinLibs
com o driver grafico instalado.
