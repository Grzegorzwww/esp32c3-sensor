#!/usr/bin/env python3
"""
Symulator licznika energii EM720 (IEC 62056-21)
Udaje licznik podłączony przez port szeregowy do ESP32.

Protokół:
  ESP32  → "/?1!\r\n"          (żądanie inicjalizacji)
  Symul. ← "/SAT6EM720<serial>\r\n"  (identyfikacja)
  ESP32  → "\x06060\r\n"       (ACK + baud 19200)
  Symul. ← \x02 <dane OBIS> \x03 <BCC>
"""

import serial
import serial.tools.list_ports
import threading
import time
import random
from datetime import datetime

# ── Kolory ANSI ──────────────────────────────────────────────────────────────
R  = "\033[31m"
G  = "\033[32m"
Y  = "\033[33m"
B  = "\033[34m"
M  = "\033[35m"
C  = "\033[36m"
W  = "\033[37m"
BO = "\033[1m"
RST = "\033[0m"

# ── Dane licznika (modyfikowalne) ─────────────────────────────────────────────
meter = {
    "serial":        "123456789",
    "kwh_import":    12345,      # kWh import total (1.8.0)
    "kwh_export":    0,          # kWh export total (2.8.0)
    "kvarh_import":  1234,       # kvarh import (3.8.0)
    "kvarh_export":  0,          # kvarh export (4.8.0)
    "power_w":       1500,       # bieżący pobór mocy (W) - symulowany
}

running = True
ser = None
request_count = 0
last_request_time = None


def list_ports():
    ports = serial.tools.list_ports.comports()
    if not ports:
        print(f"{Y}  Brak dostępnych portów szeregowych.{RST}")
        return []
    print(f"\n{BO}{C}  Dostępne porty szeregowe:{RST}")
    for i, p in enumerate(ports):
        print(f"  {BO}[{i}]{RST} {G}{p.device}{RST}  {W}{p.description}{RST}")
    return ports


def build_iec_data():
    """Buduje blok danych IEC 62056-21 z wartościami OBIS."""
    now = datetime.now()
    time_str = now.strftime("%H%M%S")
    date_str = now.strftime("%y%m%d")

    lines = [
        f"0.0.0({meter['serial']})",
        f"1.8.0({meter['kwh_import']:06d}*kWh)",
        f"2.8.0({meter['kwh_export']:06d}*kWh)",
        f"3.8.0({meter['kvarh_import']:06d}*kvarh)",
        f"4.8.0({meter['kvarh_export']:06d}*kvarh)",
        f"0.9.1({time_str})",
        f"0.9.2({date_str})",
    ]
    body = "\r\n".join(lines) + "\r\n"

    # Buduj ramkę: STX + dane + ETX + BCC
    stx = b'\x02'
    etx = b'\x03'
    data_bytes = body.encode('ascii')
    frame = stx + data_bytes + etx

    # BCC = XOR wszystkich bajtów od (ale nie wliczając) STX do ETX włącznie
    bcc = 0
    for b in data_bytes + etx:
        bcc ^= b

    return frame + bytes([bcc])


def calc_bcc(data: bytes) -> int:
    bcc = 0
    for b in data:
        bcc ^= b
    return bcc


def handle_connection(port: str, baudrate: int):
    global ser, request_count, last_request_time

    try:
        ser = serial.Serial(port, baudrate=baudrate, bytesize=8,
                            parity='N', stopbits=1, timeout=3)
        print(f"\n{G}✅ Otwarto port {BO}{port}{RST}{G} @ {baudrate} Bd{RST}")
        print(f"{W}  Czekam na żądania ESP32...{RST}\n")
    except serial.SerialException as e:
        print(f"{R}❌ Nie można otworzyć portu: {e}{RST}")
        return

    buffer = b""

    while running:
        try:
            chunk = ser.read(64)
            if not chunk:
                continue
            buffer += chunk

            # Szukaj żądania inicjalizacji: "/?1!\r\n"
            if b"/?!" in buffer or b"/?1!" in buffer:
                request_count += 1
                last_request_time = datetime.now().strftime("%H:%M:%S")
                print(f"{C}[{last_request_time}] 📡 #{request_count} Odebrano żądanie inicjalizacji{RST}")

                # Krok 1: Odpowiedź identyfikacyjna (9600 Bd)
                ident = f"/SAT6EM720{meter['serial']}\r\n"
                ser.write(ident.encode('ascii'))
                print(f"{G}  ← Wysłano identyfikację: {ident.strip()}{RST}")
                buffer = b""

                # Krok 2: Czekaj na ACK (\x06 + "060\r\n")
                # Uwaga: na linii mogą być śmieci (logi ESP-IDF z UART0) — szukamy \x06
                time.sleep(0.6)
                ack_raw = ser.read(64)
                ack_pos = ack_raw.find(b'\x06')
                if ack_pos != -1:
                    ack_data = ack_raw[ack_pos:]
                    print(f"{G}  → Odebrano ACK (pos={ack_pos}): {ack_data[:6].hex()}{RST}")
                    if ack_raw[:ack_pos]:
                        print(f"{Y}    (pominięto śmieci przed ACK: {ack_raw[:ack_pos].hex()}){RST}")
                else:
                    print(f"{Y}  ⚠ Brak \\x06 w odpowiedzi: {ack_raw.hex() if ack_raw else 'timeout'}{RST}")
                    print(f"{Y}    (prawdopodobnie UART0 używany też przez monitor ESP-IDF){RST}")

                # Krok 3: Wyślij dane pomiarowe
                time.sleep(0.3)
                frame = build_iec_data()
                ser.write(frame)

                # Pokaż co wysłano
                preview = frame[1:frame.index(b'\x03')].decode('ascii', errors='replace')
                print(f"{B}  ← Wysłano dane ({len(frame)} bajtów):{RST}")
                for line in preview.strip().split('\r\n'):
                    print(f"      {W}{line}{RST}")
                print(f"  {M}BCC: 0x{frame[-1]:02X}{RST}")
                print()

            # Wyczyść bufor jeśli za duży
            if len(buffer) > 256:
                buffer = b""

        except serial.SerialException as e:
            print(f"{R}❌ Błąd portu: {e}{RST}")
            break
        except Exception as e:
            print(f"{R}❌ Błąd: {e}{RST}")
            break

    if ser and ser.is_open:
        ser.close()
        print(f"{Y}🔌 Port zamknięty.{RST}")


def simulate_power_fluctuation():
    """Co 10s losowo zmienia bieżący pobór mocy i zwiększa licznik."""
    while running:
        time.sleep(10)
        if not running:
            break
        # Symuluj zmianę poboru mocy +/- 200W
        meter['power_w'] = max(0, meter['power_w'] + random.randint(-200, 200))
        # Zwiększ licznik kWh (1 kWh = 1000W przez 1h, więc co 10s += power/360000)
        meter['kwh_import'] += meter['power_w'] // 360000 or 0


def print_status():
    print(f"\n{BO}{C}══════════════════════════════════════{RST}")
    print(f"{BO}  Stan licznika:{RST}")
    print(f"  Serial:       {G}{meter['serial']}{RST}")
    print(f"  Import:       {G}{meter['kwh_import']} kWh{RST}")
    print(f"  Export:       {W}{meter['kwh_export']} kWh{RST}")
    print(f"  kvarh Import: {W}{meter['kvarh_import']} kvarh{RST}")
    print(f"  Moc bieżąca:  {Y}{meter['power_w']} W{RST}")
    if last_request_time:
        print(f"  Ostatnie żąd: {C}{last_request_time}{RST}  (łącznie: {request_count}x)")
    print(f"{BO}{C}══════════════════════════════════════{RST}\n")


def interactive_menu():
    global running
    print(f"\n{BO}{Y}Komendy:{RST}")
    print(f"  {BO}s{RST} - status licznika")
    print(f"  {BO}k{RST} - ustaw import kWh")
    print(f"  {BO}p{RST} - ustaw moc bieżącą (W)")
    print(f"  {BO}q{RST} - wyjście\n")

    while running:
        try:
            cmd = input(f"{BO}>{RST} ").strip().lower()
            if cmd == 'q':
                running = False
                print(f"{Y}👋 Zatrzymuję symulator...{RST}")
                break
            elif cmd == 's':
                print_status()
            elif cmd == 'k':
                val = input(f"  Nowy stan [kWh] (obecny: {meter['kwh_import']}): ").strip()
                try:
                    meter['kwh_import'] = int(val)
                    print(f"  {G}✅ Ustawiono: {meter['kwh_import']} kWh{RST}")
                except ValueError:
                    print(f"  {R}Nieprawidłowa wartość{RST}")
            elif cmd == 'p':
                val = input(f"  Nowa moc [W] (obecna: {meter['power_w']}): ").strip()
                try:
                    meter['power_w'] = int(val)
                    print(f"  {G}✅ Ustawiono: {meter['power_w']} W{RST}")
                except ValueError:
                    print(f"  {R}Nieprawidłowa wartość{RST}")
        except (EOFError, KeyboardInterrupt):
            running = False
            break


def main():
    global running

    print(f"\n{BO}{C}╔══════════════════════════════════════╗")
    print(f"║   Symulator licznika EM720 IEC62056  ║")
    print(f"╚══════════════════════════════════════╝{RST}\n")

    # Wybór portu
    ports = list_ports()
    if not ports:
        port_name = input(f"\n  Wpisz ręcznie port (np. /dev/ttyUSB0): ").strip()
    else:
        choice = input(f"\n  Wybierz numer portu lub wpisz ręcznie: ").strip()
        try:
            port_name = ports[int(choice)].device
        except (ValueError, IndexError):
            port_name = choice

    # Baudrate
    print(f"\n  {W}Baudrate (domyślnie 9600):{RST}")
    baud_input = input(f"  [Enter = 9600]: ").strip()
    baudrate = int(baud_input) if baud_input.isdigit() else 9600

    # Uruchom wątek komunikacji
    comm_thread = threading.Thread(
        target=handle_connection, args=(port_name, baudrate), daemon=True)
    comm_thread.start()

    # Uruchom wątek symulacji fluktuacji
    sim_thread = threading.Thread(target=simulate_power_fluctuation, daemon=True)
    sim_thread.start()

    print_status()

    # Menu interaktywne (główny wątek)
    interactive_menu()

    running = False
    time.sleep(0.5)


if __name__ == "__main__":
    main()
