#!/bin/bash

PORT="/dev/ttyACM0"
TIMEOUT=60  # maksymalnie czekaj 60 sekund
ERASE=0

# Sprawdź argument
if [ "$1" == "erase" ]; then
    ERASE=1
    echo "⚠️  Tryb ERASE — flash zostanie wyczyszczony przed wgraniem!"
fi

echo "⏳ Czekam na urządzenie na $PORT (max ${TIMEOUT}s)..."
echo "   (Podłącz ESP32 lub poczekaj aż się wybudzi)"

elapsed=0
while [ ! -e "$PORT" ]; do
    sleep 0.5
    elapsed=$((elapsed + 1))
    if [ $((elapsed % 4)) -eq 0 ]; then
        echo "   ... $((elapsed / 2))s / ${TIMEOUT}s"
    fi
    if [ $elapsed -ge $((TIMEOUT * 2)) ]; then
        echo "❌ Timeout! Urządzenie nie pojawiło się w ciągu ${TIMEOUT}s"
        exit 1
    fi
done

echo "✅ Urządzenie wykryte na $PORT!"
sleep 0.3  # chwila na pełną enumerację USB
echo ""

# Załaduj środowisko ESP-IDF jeśli nie jest załadowane
if ! command -v idf.py &> /dev/null; then
    echo "📦 Ładuję ESP-IDF..."
    source /home/bobik/esp/esp-idf/export.sh
fi

if [ $ERASE -eq 1 ]; then
    echo "🗑️  Czyszczę flash..."
    idf.py -p "$PORT" erase-flash
    echo "✅ Flash wyczyszczony — flashuję..."
fi

# Próbuj flashować aż się uda (urządzenie może zasnąć między próbami)
ATTEMPT=0
MAX_ATTEMPTS=10
while [ $ATTEMPT -lt $MAX_ATTEMPTS ]; do
    ATTEMPT=$((ATTEMPT + 1))
    echo "🔄 Próba flash #${ATTEMPT}/${MAX_ATTEMPTS}..."

    # Czekaj aż port znowu będzie dostępny
    echo "⏳ Czekam na port $PORT..."
    wait_elapsed=0
    while [ ! -e "$PORT" ]; do
        sleep 0.5
        wait_elapsed=$((wait_elapsed + 1))
        if [ $wait_elapsed -ge 120 ]; then
            echo "❌ Port nie wrócił — poddaję się"
            exit 1
        fi
    done
    sleep 0.3

    idf.py -p "$PORT" flash monitor
    EXIT_CODE=$?

    if [ $EXIT_CODE -eq 0 ]; then
        echo "✅ Flash zakończony sukcesem!"
        exit 0
    else
        echo "⚠️  Flash nie powiódł się (kod: $EXIT_CODE) — czekam na kolejne wybudzenie..."
    fi
done

echo "❌ Nie udało się po ${MAX_ATTEMPTS} próbach"
exit 1
