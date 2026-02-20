#include "time_control.h"

#include "esp_sntp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <sys/time.h>
    

current_time currentTime = {0, 0, 0};

esp_err_t init_time()
{
    // Konfiguracja SNTP
    ESP_LOGI("TIME", "🕐 Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    
    // Czekanie na synchronizację czasu
    int retry_count = 0;
    const int max_retries = MAX_TIME_READ_RETRY;
    
    while (esp_sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && retry_count < max_retries) {
        ESP_LOGW("TIME", "⚠️ Waiting for time synchronization... (%d/%d)", retry_count + 1, max_retries);
        vTaskDelay(pdMS_TO_TICKS(2000)); // Czekaj 2 sekundy przed kolejną próbą
        retry_count++;
    }
    
    if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        
        currentTime.hours = timeinfo.tm_hour;
        currentTime.minutes = timeinfo.tm_min;
        currentTime.seconds = timeinfo.tm_sec;
        
        ESP_LOGI("TIME", "✅ Time synchronized successfully: %04d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        return ESP_OK;
    }
    
    ESP_LOGE("TIME", "❌ Failed to synchronize time after %d attempts", max_retries);
    return ESP_FAIL;
}

esp_err_t get_current_time(current_time* out_time)
{
    if (out_time == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    out_time->hours = timeinfo.tm_hour;
    out_time->minutes = timeinfo.tm_min;
    out_time->seconds = timeinfo.tm_sec;
    
    return ESP_OK;
}

esp_err_t sync_time_with_ntp()
{
    ESP_LOGI("TIME", "🔄 Re-synchronizing time with NTP");
    
    // Restart SNTP
    esp_sntp_stop();
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_sntp_init();
    
    int retry_count = 0;
    const int max_retries = MAX_TIME_READ_RETRY;
    
    while (esp_sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && retry_count < max_retries) {
        ESP_LOGW("TIME", "⚠️ Waiting for time re-synchronization... (%d/%d)", retry_count + 1, max_retries);
        vTaskDelay(pdMS_TO_TICKS(2000));
        retry_count++;
    }
    
    if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        
        ESP_LOGI("TIME", "✅ Time re-synchronized successfully: %04d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        return ESP_OK;
    }
    
    ESP_LOGE("TIME", "❌ Failed to re-synchronize time after %d attempts", max_retries);
    return ESP_FAIL;
}



static time_t last_midnight_check = 0;

// Sprawdź czy właśnie jest 5:00
bool is_5AM_now(void)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    // Sprawdź czy to 05:00
    if (timeinfo.tm_hour == 5 && timeinfo.tm_min == 0) {

        // Sprawdź czy już nie wykonaliśmy tego dzisiaj
        if ((now - last_midnight_check) > 3600) {  // Więcej niż 1h temu
            last_midnight_check = now;
            sync_time_with_ntp();  // Opcjonalnie synchronizuj czas o 5:00

            return true;
        }
    }
    return false;
}