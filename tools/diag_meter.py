#!/usr/bin/env python3
"""
Diagnostyka głowicy IEC 62056-21 (LY001-B)
Testuje wszystkie kombinacje prędkości i parzystości
"""
import serial
import time

PORT = '/dev/ttyUSB0'

CONFIGS = [
    (300,  7, serial.PARITY_EVEN,  '300 Bd  7E1  <- standard IEC 62056-21'),
    (300,  8, serial.PARITY_NONE,  '300 Bd  8N1'),
    (2400, 7, serial.PARITY_EVEN,  '2400 Bd 7E1'),
    (9600, 7, serial.PARITY_EVEN,  '9600 Bd 7E1'),
    (9600, 8, serial.PARITY_NONE,  '9600 Bd 8N1'),
]

REQUEST = b'/?!\r\n'

print('='*55)
print('  Diagnostyka gowicy IEC 62056-21 / LY001-B')
print(f'  Port: {PORT}')
print('='*55)

for baud, bits, par, desc in CONFIGS:
    print(f'\n[TEST] {desc}')
    try:
        ser = serial.Serial(
            port=PORT,
            baudrate=baud,
            bytesize=bits,
            parity=par,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.2,
        )
    except Exception as e:
        print(f'  BLAD otwarcia: {e}')
        continue

    ser.dtr = True
    ser.rts = False
    time.sleep(0.5)
    ser.reset_input_buffer()

    hx_req = ' '.join('%02X' % b for b in REQUEST)
    print(f'  TX: [{hx_req}]  /?!<CR><LF>')
    ser.write(REQUEST)
    ser.flush()

    buf = bytearray()
    deadline = time.time() + 5.0
    while time.time() < deadline:
        chunk = ser.read(64)
        if chunk:
            buf += chunk
            hx = ' '.join('%02X' % b for b in chunk)
            asc = repr(chunk)
            print(f'  RX +{len(chunk):3d}B: [{hx}]  {asc}')

    if not buf:
        print('  -> BRAK odpowiedzi')
    else:
        print(f'  -> Lacznie {len(buf)} B odebrano')

    ser.close()
    time.sleep(0.8)

print('\n' + '='*55)
print('  Koniec diagnostyki')
print('='*55)
