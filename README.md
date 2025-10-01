# XIAO ESP32-C3 Project

Ten projekt jest szablonem dla XIAO ESP32-C3 wykorzystującym ESP-IDF framework.

## Wymagania

- ESP-IDF v4.4 lub nowszy
- XIAO ESP32-C3 board

## Kompilacja

```bash
# Ustaw środowisko ESP-IDF
. $HOME/esp/esp-idf/export.sh

# Ustaw target na ESP32-C3
idf.py set-target esp32c3

# Skonfiguruj projekt
idf.py menuconfig

# Kompiluj
idf.py build

# Flashuj na urządzenie
idf.py -p /dev/ttyACM0 flash monitor
```

## Funkcjonalność

Aktualnie projekt migocze wbudowaną diodą LED co sekundę i wypisuje informacje w konsoli.

## Pinout XIAO ESP32-C3

- GPIO2: Wbudowana dioda LED
- GPIO20: User LED (czerwona)
- GPIO21: User LED (zielona) 
- GPIO19: User LED (niebieska)

## Struktura projektu

```
├── main/
│   ├── main.c          # Główny plik aplikacji
│   └── CMakeLists.txt  # Konfiguracja komponentu
├── CMakeLists.txt      # Główna konfiguracja projektu
└── README.md           # Ten plik
```
