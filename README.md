# RoadPanel ESP32: o mapa da pista no TFT

Porte do painel JS (RoadPanel) para C++, rodando num **ESP32 DevKit (sem PSRAM)** com display
**ST7796S 480×320 SPI**. Ele tem o mesmo visual (câmera de perseguição atrás do carro, pista clara com
linhas pretas, fita azul da missão com setas andando, placas, prévia GPS na PARE e nos pontos) e a
mesma lógica (rede da pista, menor caminho, tabela `lane_guide_map`, localizador por odometria +
marcos, papéis coleta/entrega).

Ficou de fora, por decisão do projeto: som/voz, tela cheia, link para o Digital Twin, painel de
debug e editor de placas. As placas, a largada e a partida são as do painel JS
(`signs.json` padrão), já embutidas em `src/generated/`.

## Como gravar

1. VS Code + extensão **PlatformIO**. Abra esta pasta (onde está o `platformio.ini`).
2. Confira os pinos em [`src/Config.h`](src/Config.h). Eles são os mesmos do `display_tft.ino`:
   SCLK 18, MOSI 23, MISO 19, DC 2, CS 5, RST 4.
3. **Upload**. O display mostra "APEX · Mapa da pista" e entra em **Simulação** (missão A → C).

Se as cores saírem trocadas, mude `TFT_BGR`. Se saírem invertidas (fundo escuro), mude
`TFT_INVERT`. Com fios curtos, `TFT_SPI_HZ 80000000` dobra a velocidade de envio. Para medir o fps,
use `SHOW_FPS 1`.

A vista é sempre de perseguição, atrás do carro. Quando o carro para na PARE ou chega num ponto, a
câmera sobe sozinha e mostra o caminho até a próxima placa (prévia GPS); depois volta para trás do carro.

## Parada nos pontos (igual ao painel web e ao `main.py`)

Quando `nav.route` perde um ponto, o carro travou nele (`point_stop_latched`). O painel fica parado no
ponto, com a parada em âmbar ("AGUARDANDO RUN") e "Aperte o botão RUN" no cartão, até: o botão RUN do
carro (`telemetry.run_button_presses`), o `run` desligado e ligado de novo, o modo manual, ou o carro
voltar a andar com PWM > 0. Coleta e entrega alternam como em `Navigation.add_point`, corrigidas por
`command.mission_state`. Quando a bateria começa a carregar (`battery_state` 1), a malha de pontos
solta ondas verdes por 9 s e as bordas da tela ficam com fumaça verde por 2 s
(`{"panel":{"charge":true}}` simula).

## Dados do carro (serial, 115200)

O ESP32 aceita **uma linha por mensagem** em dois formatos:

- **JSON no formato do `/api/vehicle_info`** (recomendado). É o mesmo dado que o painel JS lê:
  ```json
  {"hud":{"route":"A → C","traffic_light_code":-1,"stop_active":false,"right_detour_active":false,"control_mode":"AUTONOMO"},
   "command":{"run":true,"speed":120,"servo":90,"stop":false},
   "telemetry":{"speed":0.42,"battery":84,"f_left":0,"f_right":0},
   "_ts":1712345678.1,"time":"14:32"}
  ```
  `time` é opcional (o ESP32 não tem relógio). Os campos ausentes viram padrão, como no `carLink.js`.
- **Formato antigo** `velocidade_m_s,servo` (ex.: `0.42,90`). Ele só move o carro e não traz missão nem placas.

### Simulação → modo real (só cabo USB, sem Wi-Fi nem outro sinal sem fio)

1. Ligue o ESP32 no PC do carro pelo **cabo USB**. O firmware não liga o rádio (sem Wi-Fi/Bluetooth).
2. Rode o `main.py` normalmente nesse PC.
3. No mesmo PC, rode a ponte. Ela acha a porta do ESP32 sozinha e lê a API local (`127.0.0.1`):
   ```
   pip install pyserial
   python tools/serial_bridge.py
   ```
   (`--port COM5` fixa a porta; se o cabo sair, ela espera ele voltar e continua.)
4. Na primeira linha válida o painel troca sozinho de **Simulação** (selo âmbar) para **Ao vivo**
   (selo verde). Feche a ponte e em 3 s ele mostra "Sem sinal".

Para forçar um modo, mande pela mesma serial `{"panel":{"source":"serial"}}` (real) ou
`{"panel":{"source":"sim"}}` (simulação). Depois de forçado, ele não troca mais sozinho até reiniciar.

Outros comandos opcionais:
`{"panel":{"plan":"car"}}` (trajetória pela tabela do carro em vez do caminho livre) e
`{"panel":{"sim_route":"B → A"}}` (missão da simulação: coleta em B, entrega em A; sai de onde o
carro está).

Quando chega o primeiro dado válido, o painel sai da simulação e entra em **Ao vivo**, igual ao JS.
Sem dados por 3 s, ele mostra "Sem sinal"; com o `_ts` parado, "Dados parados".

## Simulação

O carro simulado anda a `SIM_SPEED_MS` (1,0 m/s, em `src/Config.h`), sempre no sentido da pista
(mão única). Ao parar numa placa (ponto A/B/C ou PARE) a câmera abre e mostra a pista inteira,
segura essa vista enquanto o carro espera e volta para trás dele quando ele segue. A simulação não
reinicia: depois da entrega o carro completa a volta, para na linha de chegada e fica em
"Percurso finalizado" até chegar uma missão nova pela serial.

## Ambiente de testes no PC

`tools\sim\build.bat` compila a lógica e o desenho do firmware (os mesmos `.cpp`) com o MSVC e
roda em tempo virtual: confere coleta e entrega em todas as combinações de pontos, mão única (inclusive
no modo ao vivo, com marcos corrigindo a estimativa), o zoom da pista inteira, quadros lentos (~6 fps),
o fim do percurso e defeitos de desenho (nenhuma faixa escura cortando a tela). Os quadros (480×320,
como no TFT) ficam em `tools\sim\out`. Para gravar uma missão inteira quadro a quadro:
`python tools\sim\run.py tools\sim\build\roadpanel_sim_*.dll pasta frames AC 250` (um quadro a cada
250 ms). Precisa do Visual Studio com C++ e do Python.

## Como funciona

| Parte | Arquivo | Origem no JS |
|---|---|---|
| Rede, trajetória, eventos | `src/track/Network.*`, `Planner.*` | `network.js`, `planner.js`, `carLogic.js`, `mission.js` |
| Localização do carro real | `src/track/Localizer.*` | `localizer.js` |
| Estado, simulação, GPS, câmera | `src/app/App.*` | `main.js`, `cameraRig.js`, `hud.js` |
| Cena 3D | `src/render/Scene.*` | `trackMeshes.js`, `routeRibbon.js`, `gpsPreview.js`, `signs.js`, `car.js` |
| HUD | `src/render/Hud.*` | `index.html`, `styles.css` |
| Serial | `src/link/SerialParse.cpp` | `carLink.js` (`normalize`) |

O frame inteiro (300 KB) não cabe na RAM. Por isso a tela é desenhada em **10 faixas de 480×32**
que vão por DMA enquanto a próxima é desenhada, com os dois núcleos trabalhando juntos. O chão é
calculado por pixel: cada linha da tela cruza o chão numa reta, e cada ponto consulta um **campo de
distância da pista** (`track_sdf.h`). Assim as linhas saem suavizadas em qualquer zoom, como o
`fwidth` dos shaders do JS.

## Dados fixos

A pista, as placas (posição de A, B, C, PARE, desvios e semáforo), a largada, a fonte e os ícones
foram gerados uma vez a partir do painel JS e estão em `src/generated/`. Pela serial chegam só o
estado do carro: a missão (`hud.route`), as detecções e a telemetria.

O carro é o modelo 3D `3DModel/base_basic_pbr.glb`, renderizado no PC com a iluminação do painel JS e
guardado em `src/generated/car_sprites.h`. São 25 ângulos da câmera de perseguição, 13 do ângulo
intermediário e a vista de cima da prévia GPS (girada em tempo real). O ESP32 só escolhe e escala a imagem certa;
ele não desenha triângulos.
