#!/bin/bash

# =================================================================
# XIAO ESP32-C3 Flash & Monitor Script
# Autor: GitHub Copilot
# Przeznaczenie: Wgrywanie kodu na ESP32-C3 w deep sleep i monitor
# =================================================================

# Kolory dla lepszej czytelności
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Konfiguracja
PROJECT_DIR="/home/bobik/Dokumenty/workspace/esp32s3-sensor"
PORT="/dev/ttyACM0"
BAUD_RATE="460800"
CHIP="esp32c3"

# Funkcja wyświetlania nagłówka
show_header() {
    clear
    echo -e "${CYAN}=================================================================${NC}"
    echo -e "${CYAN}          🚀 XIAO ESP32-C3 Flash & Monitor Tool 🚀${NC}"
    echo -e "${CYAN}=================================================================${NC}"
    echo -e "${YELLOW}Port:${NC} $PORT"
    echo -e "${YELLOW}Chip:${NC} $CHIP"
    echo -e "${YELLOW}Baud:${NC} $BAUD_RATE"
    echo -e "${CYAN}=================================================================${NC}"
    echo ""
}

# Funkcja sprawdzania ESP-IDF
check_idf() {
    echo -e "${BLUE}📋 Sprawdzam środowisko ESP-IDF...${NC}"
    cd "$PROJECT_DIR" || exit 1
    
    # Sprawdź czy ESP-IDF jest dostępne
    if ! command -v idf.py &> /dev/null; then
        echo -e "${YELLOW}⚠️  ESP-IDF nie jest aktywne, aktywuję...${NC}"
        source "$HOME/esp/esp-idf/export.sh"
    fi
    
    if command -v idf.py &> /dev/null; then
        echo -e "${GREEN}✅ ESP-IDF jest gotowe${NC}"
    else
        echo -e "${RED}❌ Błąd: Nie można aktywować ESP-IDF${NC}"
        exit 1
    fi
}

# Funkcja kompilacji
compile_project() {
    echo -e "${BLUE}🔨 Kompilacja projektu...${NC}"
    if idf.py build; then
        echo -e "${GREEN}✅ Kompilacja zakończona pomyślnie${NC}"
    else
        echo -e "${RED}❌ Błąd kompilacji${NC}"
        exit 1
    fi
}

# Funkcja wgrywania z wykrywaniem ESP32-C3
flash_esp32() {
    echo -e "${PURPLE}🔄 Czekam na ESP32-C3...${NC}"
    echo -e "${YELLOW}💡 INSTRUKCJA:${NC}"
    echo -e "   1. Odłącz ESP32-C3 od USB"
    echo -e "   2. Poczekaj 2 sekundy"
    echo -e "   3. Podłącz ESP32-C3 z powrotem"
    echo -e "   4. Skrypt automatycznie wykryje i wgra kod"
    echo ""
    echo -e "${CYAN}⏰ Czekam maksymalnie 60 sekund...${NC}"
    
    for i in {1..60}; do
        if [ -e "$PORT" ]; then
            echo -e "${GREEN}✅ ESP32-C3 wykryty na $PORT! Wgrywam natychmiast...${NC}"
            
            python -m esptool \
                --chip "$CHIP" \
                --port "$PORT" \
                --baud "$BAUD_RATE" \
                --before default_reset \
                --after hard_reset \
                write_flash \
                --flash_mode dio \
                --flash_freq 80m \
                --flash_size 4MB \
                0x0 build/bootloader/bootloader.bin \
                0x8000 build/partition_table/partition-table.bin \
                0x10000 build/xiao_esp32c3_template.bin
            
            if [ $? -eq 0 ]; then
                echo -e "${GREEN}🎉 WGRYWANIE UDANE!${NC}"
                return 0
            else
                echo -e "${RED}❌ Błąd wgrywania, próbuję ponownie...${NC}"
            fi
        fi
        
        printf "\r${YELLOW}⏳ Czekam... (%d/60s)${NC}" "$i"
        sleep 1
    done
    
    echo -e "\n${RED}❌ Timeout: ESP32-C3 nie został wykryty w ciągu 60 sekund${NC}"
    return 1
}

# Funkcja monitora szeregowego
monitor_esp32() {
    echo -e "${BLUE}📺 Uruchamiam monitor szeregowy...${NC}"
    echo -e "${YELLOW}💡 ESP32-C3 budzi się co 30 sekund${NC}"
    echo -e "${YELLOW}   Naciśnij Ctrl+C aby zakończyć monitor${NC}"
    echo ""
    
    while true; do
        if [ -e "$PORT" ]; then
            echo -e "${GREEN}🟢 $(date '+%H:%M:%S') - ESP32-C3 OBUDZONY!${NC}"
            
            # Czytaj port szeregowy przez maksymalnie 25 sekund
            timeout 25s cat "$PORT" 2>/dev/null || true
            
            echo -e "${PURPLE}💤 $(date '+%H:%M:%S') - ESP32-C3 poszedł spać...${NC}"
            echo -e "${CYAN}----------------------------------------${NC}"
        else
            printf "\r${YELLOW}😴 $(date '+%H:%M:%S') - ESP32-C3 śpi...${NC}"
        fi
        sleep 2
    done
}

# Menu główne
show_menu() {
    echo -e "${CYAN}Wybierz opcję:${NC}"
    echo -e "${YELLOW}1)${NC} Tylko wgraj kod (flash)"
    echo -e "${YELLOW}2)${NC} Wgraj kod i uruchom monitor"
    echo -e "${YELLOW}3)${NC} Tylko monitor (bez wgrywania)"
    echo -e "${YELLOW}4)${NC} Kompiluj, wgraj i monitor (pełny cykl)"
    echo -e "${YELLOW}5)${NC} Wyjście"
    echo ""
    echo -ne "${BLUE}Twój wybór [1-5]: ${NC}"
}

# Funkcja główna
main() {
    show_header
    check_idf
    
    while true; do
        show_menu
        read -r choice
        
        case $choice in
            1)
                echo -e "${BLUE}🚀 Rozpoczynam wgrywanie...${NC}"
                flash_esp32
                ;;
            2)
                echo -e "${BLUE}🚀 Rozpoczynam wgrywanie i monitor...${NC}"
                if flash_esp32; then
                    echo -e "${BLUE}⏳ Czekam 5 sekund przed startem monitora...${NC}"
                    sleep 5
                    monitor_esp32
                fi
                ;;
            3)
                echo -e "${BLUE}📺 Uruchamiam tylko monitor...${NC}"
                monitor_esp32
                ;;
            4)
                echo -e "${BLUE}🔄 Pełny cykl: kompilacja + flash + monitor...${NC}"
                compile_project
                if flash_esp32; then
                    echo -e "${BLUE}⏳ Czekam 5 sekund przed startem monitora...${NC}"
                    sleep 5
                    monitor_esp32
                fi
                ;;
            5)
                echo -e "${GREEN}👋 Do widzenia!${NC}"
                exit 0
                ;;
            *)
                echo -e "${RED}❌ Nieprawidłowy wybór. Spróbuj ponownie.${NC}"
                sleep 2
                show_header
                ;;
        esac
        
        echo ""
        echo -e "${CYAN}=================================================================${NC}"
        echo -ne "${YELLOW}Naciśnij Enter aby kontynuować...${NC}"
        read -r
        show_header
    done
}

# Obsługa Ctrl+C
trap 'echo -e "\n${YELLOW}👋 Skrypt przerwany przez użytkownika${NC}"; exit 0' INT

# Uruchom główną funkcję
main
