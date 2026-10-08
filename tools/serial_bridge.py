"""
Ponte servidor -> ESP32: lê /api/vehicle_info (o mesmo que o painel JS lê) e manda pela serial,
um JSON por linha, só com os campos que o painel usa. Não precisa mudar nada no main.py.

    pip install pyserial
    python tools/serial_bridge.py --port COM5                  # Windows
    python tools/serial_bridge.py --port /dev/ttyUSB0 --api http://127.0.0.1:5000/api

Se preferir mandar direto do main.py, use send_vehicle_info(ser, vehicle_info) abaixo.
"""
import argparse
import json
import time
import urllib.request

import serial  # pyserial

HUD_KEYS = ('route', 'route_list', 'running', 'speed', 'servo', 'traffic_light_code', 'stop_active',
            'right_detour_active', 'control_mode')
COMMAND_KEYS = ('run', 'speed', 'servo', 'stop')
TELEMETRY_KEYS = ('speed', 'battery', 'left', 'f_left', 'f_right', 'right')


def compact(info):
    """Só o que o painel usa (linha curta = cabe folgado em 115200 baud a 5 Hz)."""
    def pick(obj, keys):
        obj = obj if isinstance(obj, dict) else {}
        return {k: obj[k] for k in keys if k in obj}
    out = {
        'hud': pick(info.get('hud'), HUD_KEYS),
        'command': pick(info.get('command'), COMMAND_KEYS),
        'telemetry': pick(info.get('telemetry'), TELEMETRY_KEYS),
        'time': time.strftime('%H:%M'),
    }
    if '_ts' in info:
        out['_ts'] = info['_ts']
    if not info.get('hud'):
        del out['hud']   # sem hud = "aguardando o carro" no painel
    return out


def send_vehicle_info(ser, info):
    line = json.dumps(compact(info), ensure_ascii=False, separators=(',', ':'))
    ser.write(line.encode('utf-8') + b'\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--port', required=True, help='porta serial do ESP32 (COM5, /dev/ttyUSB0...)')
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument('--api', default='http://127.0.0.1:5000/api', help='base da API do carro')
    parser.add_argument('--hz', type=float, default=5.0, help='mensagens por segundo (o painel JS usa 5)')
    args = parser.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0)
    time.sleep(2)   # o ESP32 reinicia quando a porta abre
    url = args.api.rstrip('/') + '/vehicle_info'
    period = 1.0 / args.hz
    print(f'{url} -> {args.port} @ {args.baud} ({args.hz:g} Hz). Ctrl+C para sair.')
    while True:
        started = time.monotonic()
        try:
            with urllib.request.urlopen(url, timeout=1.5) as response:
                send_vehicle_info(ser, json.loads(response.read().decode('utf-8')))
        except (OSError, ValueError) as error:
            # servidor fora: o painel mostra "Sem sinal" sozinho depois de 3 s
            print('sem dados:', error)
        # mostra o que o ESP32 escrever (memória livre, avisos)
        incoming = ser.read(ser.in_waiting or 0)
        if incoming:
            print(incoming.decode('utf-8', 'replace'), end='')
        time.sleep(max(0.0, period - (time.monotonic() - started)))


if __name__ == '__main__':
    main()
