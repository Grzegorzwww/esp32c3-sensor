#!/usr/bin/env python3
"""
Odczyt licznika energii elektrycznej przez IEC 62056-21 (optyczny port szeregowy)
Testowany z: EM720, SAT6EM720, LY001-B

IEC 62056-21 Mode C (standard):
  1. Polacz na 300 Bd 7E1
  2. Wyslij /?!\r\n
  3. Licznik odpowiada /MFR<Z><ident>\r\n
  4. Wyslij ACK \x060<Z>0\r\n
  5. Przelacz port na nowa predkosc (Z)
  6. Odbierz blok STX...ETX+BCC

LY001-B: konwerter optyczny USB->IEC 62056-21
"""

import serial
import serial.tools.list_ports
import time
import sys

# === KONFIGURACJA ===
INIT_BAUD  = 300      # IEC 62056-21 zawsze startuje na 300 Bd
TIMEOUT    = 5        # sekundy

# Mapa predkosci wg IEC 62056-21 (Z w ACK)
BAUD_MAP = {
    '0': 300,
    '1': 600,
    '2': 1200,
    '3': 2400,
    '4': 4800,
    '5': 9600,
    '6': 19200,
}
# =====================

def dbg(label, data):
    if isinstance(data, (bytes, bytearray)):
        hex_str  = ' '.join(f'{b:02X}' for b in data)
        ascii_str = data.decode('ascii', errors='replace').replace('\r','<CR>').replace('\n','<LF>')
        print(f"   [DBG] {label}: hex=[{hex_str}]  ascii='{ascii_str}'")
    else:
        print(f"   [DBG] {label}: {data}")

def list_ports():
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("Brak dostepnych portow szeregowych!")
        return []
    print("\nDostepne porty:")
    for i, p in enumerate(ports):
        print(f"  [{i}] {p.device}")
        print(f"       Opis:   {p.description}")
        print(f"       HWID:   {p.hwid}")
        if p.vid:
            print(f"       VID:PID {p.vid:04X}:{p.pid:04X}")
    return ports

def open_port(port_name, baud, parity=serial.PARITY_EVEN, bytesize=serial.SEVENBITS):
    params = f"{baud}Bd  {bytesize}{'E' if parity==serial.PARITY_EVEN else 'N'}1"
    print(f"   -> Otwieranie {port_name} @ {params}")
    try:
        ser = serial.Serial(
            port=port_name,
            baudrate=baud,
            bytesize=bytesize,
            parity=parity,
            stopbits=serial.STOPBITS_ONE,
            timeout=TIMEOUT,
            xonxoff=False,
            rtscts=False,
        )
        return ser
    except serial.SerialException as e:
        print(f"   BLAD otwarcia portu: {e}")
        return None

def read_until_crlf(ser, timeout_s=5.0):
    buf = bytearray()
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        b = ser.read(1)
        if b:
            buf += b
            if buf.endswith(b'\r\n'):
                break
        else:
            if buf:
                break
    return bytes(buf)

def read_data_block(ser, timeout_s=10.0):
    STX = 0x02
    ETX = 0x03
    buf = bytearray()
    deadline = time.time() + timeout_s
    found_stx = False
    while time.time() < deadline:
        b = ser.read(1)
        if not b:
            if buf:
                print(f"   TIMEOUT w trakcie odczytu, zebrano {len(buf)} bajtow")
            break
        buf += b
        if not found_stx and b[0] == STX:
            found_stx = True
        if found_stx and len(buf) >= 2 and buf[-2] == ETX:
            bcc = ser.read(1)
            if bcc:
                buf += bcc
            break
    return bytes(buf)

def calc_bcc(data):
    bcc = 0
    in_block = False
    for b in data:
        if b == 0x02:
            in_block = True
        if in_block:
            bcc ^= b
        if b == 0x03:
            break
    return bcc

def parse_obis(raw):
    obis_map = {
        '1.8.0':  ('Import kWh (calkowity)', 'kWh'),
        '2.8.0':  ('Export kWh (calkowity)', 'kWh'),
        '1.8.1':  ('Taryfa T1',              'kWh'),
        '1.8.2':  ('Taryfa T2',              'kWh'),
        '3.8.0':  ('Import kvarh',           'kvarh'),
        '4.8.0':  ('Export kvarh',           'kvarh'),
        '0.9.1':  ('Czas licznika',          ''),
        '0.9.2':  ('Data licznika',          ''),
        '0.0.0':  ('Numer seryjny',          ''),
        '96.1.0': ('ID urzadzenia',          ''),
    }
    try:
        text = raw.decode('ascii', errors='replace')
    except:
        text = raw.decode('latin-1', errors='replace')

    print("\nSurowe linie danych:")
    lines = text.splitlines()
    for line in lines:
        if line.strip():
            print(f"  | {line}")

    print("\nOdczytane wartosci OBIS:")
    found_any = False
    for line in lines:
        for code, (label, unit) in obis_map.items():
            if code + '(' in line:
                start = line.find('(') + 1
                end   = line.find(')')
                if start > 0 and end > start:
                    value = line[start:end]
                    if '*' in value:
                        value = value.split('*')[0]
                    print(f"   {label}: {value} {unit}")
                    found_any = True
    if not found_any:
        print("   BRAK kodow OBIS w danych")

def read_meter_mode_c(port_name):
    print(f"\n{'='*55}")
    print(f" IEC 62056-21 Mode C  port={port_name}")
    print(f"{'='*55}")

    print(f"\n[KROK 1] Otwieram {port_name} na 300 Bd 7E1...")
    ser = open_port(port_name, INIT_BAUD, parity=serial.PARITY_EVEN, bytesize=serial.SEVENBITS)
    if ser is None:
        return

    try:
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        # DTR zasila niektorych glowice optyczne przez USB
        ser.dtr = True
        ser.rts = False
        print(f"   DTR={ser.dtr}  RTS={ser.rts}")
        time.sleep(0.5)

        # Sprawdz czy sa juz jakies dane (echo lub szum)
        waiting = ser.in_waiting
        if waiting:
            garbage = ser.read(waiting)
            dbg("RX (szum przed requestem)", garbage)
            ser.reset_input_buffer()

        print(f"\n[KROK 2] Wysylam request: /?!<CR><LF>")
        req = b'/?!\r\n'
        dbg("TX", req)
        ser.write(req)
        ser.flush()

        print(f"\n[KROK 3] Czekam na identyfikacje (timeout={TIMEOUT}s)...")
        ident = read_until_crlf(ser, timeout_s=TIMEOUT)
        dbg("RX ident", ident)

        if not ident:
            print("   BRAK ODPOWIEDZI!")
            print("   Mozliwe przyczyny:")
            print("   1. Glowica nie jest przylozona do licznika (sprawdz wyrownanie LED)")
            print("   2. Zly port - czy LED glowicy miga po wyslaniu requestu?")
            print("   3. DTR nie zasila glowicy - sprobuj z DTR=False lub wlasnym zasilaniem")
            print("   4. Licznik wymaga inicjalizacji na innej predkosci")
            return False

        if b'/' not in ident:
            print(f"   OSTRZEZENIE: brak znaku '/' w odpowiedzi")
            print(f"   Moze to byc echo requestu lub szum. Czekam na wiecej danych...")
            extra = ser.read(128)
            if extra:
                dbg("RX extra", extra)
                ident = ident + extra
            if b'/' not in ident:
                print("   Nadal brak '/' - licznik nie odpowiada poprawnie")
                return False

        ident_text = ident.decode('ascii', errors='replace').strip()
        print(f"   OK! Identyfikacja: {ident_text}")

        # Wyciagnij Z (kod predkosci) - 5. znak po '/'
        baud_char = '5'  # domyslnie 9600
        slash_idx = ident.find(b'/')
        if slash_idx >= 0 and len(ident) > slash_idx + 4:
            baud_char = chr(ident[slash_idx + 4])
            new_baud = BAUD_MAP.get(baud_char, 9600)
            print(f"   Kod predkosci Z='{baud_char}' -> {new_baud} Bd")
        else:
            print(f"   Nie udalo sie odczytac kodu predkosci, uzywam Z='5' (9600 Bd)")

        # [KROK 4] Wyslij ACK
        ack = bytes([0x06]) + f'0{baud_char}0\r\n'.encode()
        print(f"\n[KROK 4] Wysylam ACK (V=0, Z={baud_char}, Y=0)...")
        dbg("TX ACK", ack)
        ser.write(ack)
        ser.flush()

        # [KROK 5] Przelacz predkosc
        new_baud = BAUD_MAP.get(baud_char, 9600)
        print(f"\n[KROK 5] Przelaczam na {new_baud} Bd (200ms przerwa normatywna)...")
        time.sleep(0.22)
        ser.baudrate = new_baud
        time.sleep(0.1)
        print(f"   Aktualna predkosc: {ser.baudrate} Bd  parity=7E1")

        # [KROK 6] Odczytaj blok danych
        print(f"\n[KROK 6] Czekam na blok danych STX...ETX+BCC (timeout=10s)...")
        raw = read_data_block(ser, timeout_s=10.0)

        if not raw:
            print("   BRAK BLOKU DANYCH!")
            print("   ACK mogl nie dotrzec, lub predkosc zle przestawiona")
            return False

        print(f"   Odebrano {len(raw)} bajtow")
        dbg("RX data (64B)", raw[:64])

        # Sprawdz BCC
        if 0x03 in raw:
            etx_idx = raw.rindex(0x03)
            if etx_idx + 1 < len(raw):
                bcc_recv = raw[etx_idx + 1]
                bcc_calc = calc_bcc(raw)
                ok = "OK" if bcc_recv == bcc_calc else "BLAD"
                print(f"   BCC: odebrano=0x{bcc_recv:02X}  obliczono=0x{bcc_calc:02X}  [{ok}]")

        print("\n" + "-"*55)
        parse_obis(raw)
        print("-"*55)
        return True

    finally:
        ser.close()
        print(f"\nPort {port_name} zamkniety")

def read_meter_8n1(port_name):
    """Tryb awaryjny: 8N1 @ 9600 Bd (niektorych chinczykow)."""
    print(f"\n{'='*55}")
    print(f" TRYB AWARYJNY: 8N1 @ 9600 Bd  port={port_name}")
    print(f"{'='*55}")
    ser = open_port(port_name, 9600, parity=serial.PARITY_NONE, bytesize=serial.EIGHTBITS)
    if ser is None:
        return

    try:
        ser.reset_input_buffer()
        ser.dtr = True
        ser.rts = False
        time.sleep(0.3)

        req = b'/?!\r\n'
        print(f"Wysylam /?!<CR><LF>...")
        dbg("TX", req)
        ser.write(req)
        ser.flush()
        time.sleep(1.5)

        raw = ser.read(256)
        if raw:
            dbg("RX 8N1", raw)
            try:
                print("Tresc:", raw.decode('ascii', errors='replace'))
            except:
                pass
        else:
            print("BRAK odpowiedzi w trybie 8N1")
    finally:
        ser.close()

# === MAIN ===
if __name__ == '__main__':
    print("=" * 55)
    print(" Odczyt licznika IEC 62056-21  (LY001-B / EM720)")
    print(" Wersja DEBUG")
    print("=" * 55)

    ports = list_ports()
    if not ports:
        sys.exit(1)

    if len(ports) == 1:
        choice = 0
        print(f"\nAutomatycznie wybrany: {ports[0].device}")
    else:
        try:
            choice = int(input("\nWybierz numer portu: "))
        except (ValueError, KeyboardInterrupt):
            print("Anulowano")
            sys.exit(1)

    if choice < 0 or choice >= len(ports):
        print("Nieprawidlowy wybor")
        sys.exit(1)

    port_name = ports[choice].device

    # Proba 1: standard IEC 62056-21 Mode C (300 Bd 7E1)
    success = read_meter_mode_c(port_name)

    if not success:
        print(f"\n" + "-" * 55)
        ans = input("Sprobowac trybu awaryjnego 8N1 @ 9600 Bd? [t/N]: ").strip().lower()
        if ans == 't':
            read_meter_8n1(port_name)
