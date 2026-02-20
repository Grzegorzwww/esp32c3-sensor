# 🔋 WiFi Power Save - Przykłady użycia

## Tryb 1: WiFi Modem Sleep (Automatyczny - Już włączony!) ⭐

**Najlepszy dla większości aplikacji** - oszczędza energię bez dodatkowej konfiguracji.

### Jak działa:
- WiFi radio **wyłącza się między pakietami**
- Router wie, że ESP śpi i buforuje pakiety
- ESP budzi się na beacon (co ~100ms) aby odebrać dane
- **WiFi pozostaje połączone przez cały czas**

### Zużycie energii:
- **Bez Modem Sleep**: 70-100 mA
- **Z Modem Sleep**: 20-30 mA
- **Oszczędności**: ~50-70 mA (66-70%)

### Kod (już działa w Twoim projekcie):
```c
// W communication.c - automatycznie włączane po połączeniu WiFi
esp_wifi_set_ps(WIFI_PS_MIN_MODEM);  // ✅ Już jest!
```

### Warianty:
```c
// Opcja 1: Minimalne oszczędzanie (lepsze dla czułych na opóźnienia)
esp_wifi_set_ps(WIFI_PS_MIN_MODEM);  // ⭐ ZALECANE

// Opcja 2: Maksymalne oszczędzanie (większe opóźnienia, ale więcej oszczędności)
esp_wifi_set_ps(WIFI_PS_MAX_MODEM);

// Opcja 3: Wyłącz (tylko dla testów!)
esp_wifi_set_ps(WIFI_PS_NONE);  // ❌ NIE UŻYWAJ - marnuje energię
```

---

## Tryb 2: Dynamic Frequency Scaling (DFS)

**Dla zaawansowanych użytkowników** - zwalnia CPU gdy nie ma pracy.

### Jak działa:
- CPU automatycznie zmienia częstotliwość: 40 MHz ↔ 160 MHz
- Gdy są zadania do wykonania → 160 MHz (pełna moc)
- Gdy idle → 40 MHz (oszczędzanie)

### Zużycie energii:
- **CPU 160 MHz**: ~50-60 mA (bez WiFi)
- **CPU 40 MHz**: ~15-20 mA (bez WiFi)
- **Z WiFi Modem Sleep + DFS**: ~15-20 mA średnio

### Konfiguracja:

#### 1. Włącz w menuconfig:
```bash
idf.py menuconfig
# Component config → Power Management
#   [*] Enable Power Management (CONFIG_PM_ENABLE)
#   [*] Enable dynamic frequency scaling (CONFIG_PM_DFS_INIT_AUTO)
```

#### 2. Wywołaj w kodzie:
```c
// W main.c po połączeniu WiFi:
communication_enable_advanced_power_save(false);  // DFS bez Light Sleep
```

### Uwagi:
- ⚠️ Wymaga `CONFIG_PM_ENABLE=y` w sdkconfig
- ✅ Nie wpływa na WiFi - działa razem z Modem Sleep
- ✅ Bezpieczne dla UART/timery (automatycznie zwiększa CPU przy przerwaniach)

---

## Tryb 3: Light Sleep + DFS (Najbardziej zaawansowany)

**Maksymalne oszczędności** - CPU całkowicie zasypia gdy nie ma pracy.

### Jak działa:
- CPU i RAM **całkowicie wyłączane** gdy brak aktywności
- WiFi pozostaje połączone (dzięki Modem Sleep)
- Budzi się na:
  - WiFi beacon (~100ms)
  - UART przerwanie
  - GPIO przerwanie
  - Timer

### Zużycie energii:
- **WiFi aktywne + Light Sleep**: ~5-10 mA
- **Oszczędności vs normalny tryb**: 85-90%!

### Konfiguracja:

#### 1. Włącz w menuconfig:
```bash
idf.py menuconfig
# Component config → Power Management
#   [*] Enable Power Management (CONFIG_PM_ENABLE)
#   
# Component config → FreeRTOS
#   [*] Tickless idle support (CONFIG_FREERTOS_USE_TICKLESS_IDLE)
```

#### 2. Wywołaj w kodzie:
```c
// W main.c po połączeniu WiFi:
communication_enable_advanced_power_save(true);  // DFS + Light Sleep
```

### ⚠️ WAŻNE dla UART!

Jeśli używasz UART do odbioru danych, **musisz zablokować Light Sleep** podczas nasłuchiwania:

```c
#include "esp_pm.h"

static esp_pm_lock_handle_t uart_lock;

void init_uart_with_pm()
{
    init_uart();  // Twoja funkcja
    
    // Stwórz lock zapobiegający Light Sleep podczas odbioru UART
    esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "uart_rx", &uart_lock);
}

void uart_event_task(void *pvParameters)
{
    // Zablokuj Light Sleep - UART musi być aktywny
    esp_pm_lock_acquire(uart_lock);
    
    while (1) {
        if (xQueueReceive(uart_queue, &event, portMAX_DELAY)) {
            // Przetwarzaj dane UART...
        }
    }
    
    // (Opcjonalnie) Zwolnij lock gdy UART nie jest używany
    // esp_pm_lock_release(uart_lock);
}
```

**Dlaczego?** 
- Light Sleep wyłącza zegary peryferii
- UART nie będzie odbierać danych podczas snu
- Lock mówi systemowi: "nie śpij, czekam na UART"

---

## Przykład kompletnej konfiguracji dla Twojego projektu

```c
// main.c
void app_main(void)
{
    ESP_LOGI("MAIN", "🚀 Starting ESP32-C3 Sensor");
    
    // 1. Inicjalizacja komunikacji
    ESP_ERROR_CHECK(communication_init());
    
    // 2. Połącz WiFi
    if (communication_connect_wifi()) {
        ESP_LOGI("MAIN", "✅ WiFi connected");
        // ✅ WiFi Modem Sleep już włączony automatycznie!
        
        // 3. Połącz MQTT
        communication_connect_mqtt();
    }
    
    // 4. (OPCJONALNIE) Włącz zaawansowane oszczędzanie
    // Wybierz JEDNĄ z opcji:
    
    // Opcja A: Tylko DFS (bezpieczne dla UART)
    // communication_enable_advanced_power_save(false);
    
    // Opcja B: DFS + Light Sleep (wymaga UART lock!)
    // communication_enable_advanced_power_save(true);
    
    // 5. Inicjalizacja UART z PM lock (jeśli używasz Light Sleep)
    init_uart();
    
    // 6. Reszta aplikacji...
    while (1) {
        // Twoja logika
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

---

## Porównanie trybów - szybkie podsumowanie

| Tryb | Kod | Pobór | WiFi | UART | Łatwość |
|------|-----|-------|------|------|---------|
| **Modem Sleep** | `esp_wifi_set_ps(WIFI_PS_MIN_MODEM)` | 20-30 mA | ✅ | ✅ | ⭐⭐⭐ |
| **DFS + Modem** | `communication_enable_advanced_power_save(false)` | 15-20 mA | ✅ | ✅ | ⭐⭐ |
| **Light + DFS** | `communication_enable_advanced_power_save(true)` | 5-10 mA | ✅ | ⚠️ Lock! | ⭐ |
| **Bez PM** | `esp_wifi_set_ps(WIFI_PS_NONE)` | 70-100 mA | ✅ | ✅ | ⭐⭐⭐ |

---

## Sprawdzenie obecnego trybu

```c
#include "esp_wifi.h"

void check_power_save_mode()
{
    wifi_ps_type_t ps_mode;
    esp_wifi_get_ps(&ps_mode);
    
    switch (ps_mode) {
        case WIFI_PS_NONE:
            ESP_LOGI("PM", "Power Save: NONE (70-100 mA)");
            break;
        case WIFI_PS_MIN_MODEM:
            ESP_LOGI("PM", "Power Save: MIN_MODEM (20-30 mA) ✅");
            break;
        case WIFI_PS_MAX_MODEM:
            ESP_LOGI("PM", "Power Save: MAX_MODEM (15-25 mA)");
            break;
        default:
            ESP_LOGI("PM", "Power Save: Unknown (%d)", ps_mode);
    }
}
```

---

## Debugging - pomiar rzeczywistego poboru prądu

### Metoda 1: Logowanie zużycia energii
```c
#include "esp_pm.h"

void log_pm_locks()
{
    // Wypisz aktywne PM locks
    esp_pm_dump_locks(stdout);
}
```

### Metoda 2: Multimetr
1. Odłącz USB
2. Podłącz multimetr szeregowo z baterią
3. Zmierz prąd w mA
4. Porównaj z tabelą powyżej

### Typowe wartości dla ESP32-C3:
- **Idle bez PM**: 70-100 mA
- **WiFi Modem Sleep**: 20-30 mA  ⭐
- **DFS + Modem**: 15-20 mA
- **Light Sleep + Modem**: 5-10 mA
- **Deep Sleep**: 0.01-0.1 mA (bez WiFi!)

---

## Rekomendacje dla Twojego projektu

### Jeśli używasz UART do odczytu czujnika prądu:

**✅ ZALECANE: WiFi Modem Sleep (już działa!)**
```c
// Nic nie musisz robić - już jest włączone!
// Pobór: ~20-30 mA
// WiFi: ✅ Działa
// UART: ✅ Działa bez problemów
```

### Jeśli UART jest rzadko używany:

**✅ OPCJONALNE: DFS + Modem Sleep**
```c
// W main.c po communication_connect_wifi():
communication_enable_advanced_power_save(false);
// Pobór: ~15-20 mA
// WiFi: ✅ Działa
// UART: ✅ Działa
```

### Jeśli chcesz maksymalne oszczędności:

**⚠️ ZAAWANSOWANE: Light Sleep + PM Lock dla UART**
```c
// 1. Włącz w menuconfig: CONFIG_PM_ENABLE + CONFIG_FREERTOS_USE_TICKLESS_IDLE
// 2. W main.c:
communication_enable_advanced_power_save(true);
// 3. Stwórz PM lock w uart_event_task()
// Pobór: ~5-10 mA
// WiFi: ✅ Działa
// UART: ⚠️ Wymaga lock!
```

---

## FAQ

**Q: Czy WiFi się rozłącza z Modem Sleep?**  
A: ❌ NIE! WiFi pozostaje połączone. Tylko radio wyłącza się między pakietami.

**Q: Czy mogę używać MQTT z Modem Sleep?**  
A: ✅ TAK! MQTT działa normalnie. Może tylko mieć lekko większe opóźnienia (~100ms).

**Q: Który tryb jest najlepszy?**  
A: ⭐ **WiFi Modem Sleep** (już masz!) - daje 66% oszczędności bez komplikacji.

**Q: Czy Light Sleep może przegapić dane UART?**  
A: ✅ NIE, jeśli użyjesz PM lock. Bez lock - ❌ TAK, może przegapić.

**Q: Jak sprawdzić, czy działa?**  
A: Zmierz prąd multimetrem lub zobacz logi - powinno być "Modem Sleep enabled".

---

## Podsumowanie

1. **WiFi Modem Sleep** - ✅ Już włączony automatycznie w Twoim kodzie!
2. **DFS** - Opcjonalnie dla dodatkowych oszczędności
3. **Light Sleep** - Tylko dla zaawansowanych, wymaga PM locks

**Twój obecny kod już oszczędza 66% energii! 🎉**
