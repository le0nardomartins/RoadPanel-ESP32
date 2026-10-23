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
3. **Upload**. O display mostra a logo do time enquanto monta a pista e fica nela até chegarem os
   dados do carro pela serial (não há modo simulação).

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
`command.mission_state`.

Enquanto espera, a tela inteira vira a **tela da carga** (a mesma do painel web, sem voz): céu claro
com a malha de pontos, uma caixa 3D caindo na caçamba (coleta, azul) ou saindo dela (entrega, verde),
um toque apertando o botão azul (o RUN do carro) e, em letras grandes, **COLOQUE A CARGA** / **RETIRE A CARGA** e
"E APERTE O BOTÃO ● AZUL". Ela entra e sai em 260 ms; aberta, a cena 3D nem é calculada.
O título usa `src/generated/font_cargo.h` (Saira 700 condensada), gerada por
`python tools/gen_cargo_font.py` a partir da fonte do painel web (precisa de Pillow, fontTools e brotli).

Quando a bateria começa a carregar (`battery_state` 1), o mapa fecha (mesma transição de 260 ms da tela
da carga) e o display mostra só a **tela da bateria**: um símbolo de bateria com o nível em verde
(enche do zero ao abrir, um brilho corre por ele e o próximo trecho pisca), um raio no meio e a
porcentagem grande (`telemetry.battery`) com "CARREGADOR CONECTADO". Ela fica aberta enquanto carrega
e fecha quando o `battery_state` sai de 1 (carregada, desligada) ou o link cai.

## Sem dados: tela da logo

Sem dados do carro (ligando, cabo solto, servidor no ar sem o `main.py` ou `_ts` parado por 3 s) a
tela mostra só a **logo do time** no centro, com uma linha discreta embaixo dizendo o que falta
("Conectando…", "Aguardando o carro", "Sem sinal do carro", "Dados parados"). Quando os dados chegam,
ela sai em 260 ms e o mapa aparece; se eles param, ela volta. Com a logo na tela a cena 3D nem é calculada.

A logo vem de `assets/logo.png`: `python tools/gen_logo.py` (precisa de Pillow) recorta a área
desenhada, redimensiona para 352×112 (cabe com folga nos 480×320) e grava em
`src/generated/logo.h` como máscara de alfa 4 bpp em **base64**. O firmware decodifica uma vez ao
ligar (`src/render/Logo.cpp`) e pinta na cor da tinta do painel. Trocou a imagem? Rode o script de novo.

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

### Ligando no carro (só cabo USB, sem Wi-Fi nem outro sinal sem fio)

1. Ligue o ESP32 no PC do carro pelo **cabo USB**. O firmware não liga o rádio (sem Wi-Fi/Bluetooth).
2. Rode o `main.py` normalmente nesse PC.
3. No mesmo PC, rode a ponte. Ela acha a porta do ESP32 sozinha e lê a API local (`127.0.0.1`):
   ```
   pip install pyserial
   python tools/serial_bridge.py
   ```
   (`--port COM5` fixa a porta; se o cabo sair, ela espera ele voltar e continua.)
4. Na primeira linha válida a logo sai e o mapa aparece **Ao vivo** (selo verde). Feche a ponte e
   em 3 s a logo volta.

Comando opcional: `{"panel":{"plan":"car"}}` (trajetória pela tabela do carro em vez do caminho livre).

Alterar os pontos de interesse (Resetar Rota, adicionar um ponto ou trocar a lista no painel de controle)
recomeça a missão: os pontos antigos somem da tela e o carro volta para a partida. Só a saída do
primeiro ponto da lista, com o carro chegando nele, conta como ponto confirmado.


## Ambiente de testes no PC

`tools\sim\build.bat` compila a lógica e o desenho do firmware (os mesmos `.cpp`) com o MSVC e
roda em tempo virtual. Um carro falso manda os dados como o `main.py` pela serial (anda na pista,
para na PARE, tira o ponto de `nav.route` ao chegar e trava até o botão RUN) e o teste confere: a tela
da logo sem dados, coleta e entrega em todas as combinações de pontos, mão única (inclusive com a
telemetria errando a velocidade e os marcos corrigindo a estimativa), o zoom da pista inteira, quadros
lentos (~6 fps), o fim do percurso, a troca de pontos, as telas da carga e da bateria e defeitos de
desenho (nenhuma faixa escura cortando a tela). As capturas de `screenshots/` saem daqui. Os quadros (480×320,
como no TFT) ficam em `tools\sim\out`. Para gravar uma missão inteira quadro a quadro:
`python tools\sim\run.py tools\sim\build\roadpanel_sim_*.dll pasta frames AC 250` (um quadro a cada
250 ms). Precisa do Visual Studio com C++ e do Python.

## Como funciona

| Parte | Arquivo | Origem no JS |
|---|---|---|
| Rede, trajetória, eventos | `src/track/Network.*`, `Planner.*` | `network.js`, `planner.js`, `carLogic.js`, `mission.js` |
| Localização do carro real | `src/track/Localizer.*` | `localizer.js` |
| Estado, missão, GPS, câmera | `src/app/App.*` | `main.js`, `cameraRig.js`, `hud.js` |
| Cena 3D | `src/render/Scene.*` | `trackMeshes.js`, `routeRibbon.js`, `gpsPreview.js`, `signs.js`, `car.js` |
| HUD, telas da carga, da bateria e da logo | `src/render/Hud.*`, `Logo.*` | `index.html`, `styles.css` |
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
