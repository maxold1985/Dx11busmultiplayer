# Fisica raycast (6 rodas) e direcao do DX11BusMultiplayer

## Arquivo principal

`src/simulation.hpp` implementa fisica autoritativa C++11, sem Win32. O servidor
executa `sim::step(dynamics,1.0f/60.0f)`; o cliente recebe `BusState` por UDP.
Nao e um motor de fisica rigida completo: raycasts consultam o mapa de altura
`sim::terrain(x,z)` (ruas e lombada procedural), **nao** triangulos arbitrarios
de cenarios 3D importados.

## Suspensao 6 rodas

- Eixo dianteiro: indices 0 e 1 (`z=+2.70`)
- Eixo intermediario: indices 2 e 3 (`z=-1.20`)
- Eixo traseiro: indices 4 e 5 (`z=-2.65`)
- Cada ponto usa `wheelRay` para consultar o solo verticalmente.
- `springStiffness` e `springDamping` determinam mola e amortecimento;
  `SUSPENSION_SAG` define compressao do nivel de repouso.
- Distribuicao nominal de carga entre eixos: **43% / 24% / 33%**.
- Cada eixo possui anti-roll para reduzir rolamento lateral; o chassi simula
  arfagem na frenagem/aceleracao e rolagem nas curvas.
- Aderencia disponivel (tracao, freio e curva) deriva da forca normal das rodas.
  Se nao ha contato, o veiculo nao ganha tracao de pneus.
- `sim::visualTravel(BusState,roda)` reconstitui o curso pelo estado recebido
  e aciona as rodas do modelo nativo `.3ds`.

## Direcao

- `steerLimit(velocidade)`: angulo maximo menor em velocidades altas.
- `wheelSteerAngle(BusState,indice)`: Ackermann para os dois pneus da
  frente; o pneu interno faz angulo maior.
- `Dynamics::yawRate`: resposta de guinada progressiva; usa entre-eixos
  `STEER_WHEELBASE`, subesterco e limite `MAX_LATERAL_ACCEL`.
- `Dynamics::lateralSpeed`: resposta lateral simplificada dos pneus.
- Nao ha simulacao independente de temperatura/desgaste dos pneus, ABS,
  distribuicao dinamica sofisticada de torque ou flexibilidade estrutural.

## Modo de direcao OMSI 2 aproximado (tecla Ç)

O cliente reconhece a tecla **Ç** usando o layout de teclado ativo do
Windows (incluindo ABNT2). O botao na interface **Direcao OMSI: ON/OFF (Ç)**
faz a mesma alternancia, com indicacao confirmada pelo servidor.

O perfil e uma **aproximacao ajustavel**, nao uma reproducao de algoritmos
internos do OMSI 2:

- Modo classico (padrao): angulo maximo `0.53 / (1 + 0.0035*v*v)` rad,
  esterco suavizado a taxa de `3.8/s`, resposta de guinada de `3.5/s`.
- Modo OMSI aproximado: angulo maximo `0.70 / (1 + 0.0038*v*v)` rad;
  o volante avanca gradualmente (1.8 unidades normalizadas por segundo
  em repouso) e retorna mais rapido ao centro (2.3/s), com resposta
  de guinada de `4.8/s`.
- Os dois perfis preservam Ackermann, massa, suspensao raycast,
  limite de aceleracao lateral e o bloqueio de tracao sem contato.
- O servidor recebe a flag `INPUT_TOGGLE_OMSI_STEERING` (valor 32)
  e replica `BusState::steeringMode` para todos os clientes.
  **Cada jogador alterna apenas o seu proprio onibus.**
- O comando `R` (reset para origem) preserva o modo de direcao escolhido.

O protocolo de rede passa para `BUS3` com o novo campo serializado.
**Recompile e reinicie o servidor e todos os clientes**, pois versoes
anteriores nao sao compativeis com este protocolo.

## Parametros de ajuste

| Constante / local | Valor inicial | O que altera |
|---|---:|---|
| `BUS_MASS` | 12000 kg | massa suspensa do onibus |
| `SUSPENSION_SAG` | 0.20 m | compressao estatica |
| `SPRING_REST` | 0.88 m | curso maximo usado pelo raycast |
| `springDamping()` | 0.90 x sqrt(k*m) | amortecimento de cada roda |
| `antiRoll` em `suspension()` | 27000 N/m | resistencia de inclinacao por eixo |
| `STEER_WHEELBASE` | 5.0 m | distancia entre-eixos do modelo dinamico |
| `STEER_TRACK` | 2.08 m | bitola das rodas |
| `steerLimit()` | 0.53 rad / (1+0.0035*v*v) | esterco reduzido com velocidade |
| `MAX_LATERAL_ACCEL` | 3.25 m/s2 | limite de aceleracao lateral |
| `d.yawRate` em `step()` | taxa 3.5/s | rapidez da guinada |
| `d.lateralSpeed` em `step()` | taxa 4.5/s | amortecimento do deslizamento lateral |

Nao altere parametros ao acaso em sistemas multiplayer: o servidor e autoritativo.
Para testar uma mudanca, recompile e reinicie **`bus_server.exe`** e
**`bus_client.exe`**, de preferencia no mesmo commit.

## Testes

```bat
git pull origin mainn
set "BUS_WITH_ASSIMP=OFF"
build_mingw32.bat
ctest --test-dir build_mingw32 --output-on-failure
```

Os alvos `suspension_steering_tests` e `omsi_steering_tests` verificam carroceria nivelada,
compressao/amortecimento, raycasts sem colisao, angulos Ackermann,
limite lateral, transferencia de carga na frenagem, marcha a re e perda
de tracao sem contato. O build DX11 completo ainda depende do ambiente
WinLibs MinGW32/DirectX SDK do usuario.
