#ifndef CONFIG_H
#define CONFIG_H

// ===== KONFIGURACJA UŻYTKOWNIKA =====
// 
// 🔧 INSTRUKCJA PRZEŁĄCZANIA KONFIGURACJI:
// 
// 1️⃣ DLA BOBIKA (domyślnie):
//    Zostaw poniższą linię zakomentowaną
//    // #define PAULINA
//
// 2️⃣ DLA PAULINY:
//    Odkomentuj poniższą linię (usuń //)
//    #define BOBIK 1
#define WESOLA 1
//
// Po zmianie wykonaj: idf.py build flash
//

// Odkomentuj dla konfiguracji Pauliny:
// #define PAULINA

// ===== INFORMACJE O KONFIGURACJACH =====
//
// 🏠 KONFIGURACJA BOBIKA:
//   WiFi: FunBox2-9877
//   MQTT: polnocna27@3a740c0f200c45698faee4ba7744b88c.s2.eu.hivemq.cloud
//   Topiki: bme680/temperature, bme680/pressure, bme680/humidity, bme680/air_quality
//
// 💝 KONFIGURACJA PAULINY:
//   WiFi: Dom
//   MQTT: paulina@a51fd01c7c0b4e2b881c011bfbc0d781.s2.eu.hivemq.cloud
//   Topiki: bme680/temperature, bme680/pressure, bme680/humidity, bme680/air_quality
//

#endif // CONFIG_H
