#include "time_control.h"  // zawiera config.h i LOG_LOCAL_LEVEL
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <sys/time.h>
    
#define LOG_LOCAL_LEVEL ESP_LOG_NONE

current_time currentTime = {0, 0, 0};

esp_err_t init_time()
{
    // SNTP jest już uruchomiony przez sync_time_from_ntp() - tutaj tylko odczytujemy czas
    ESP_LOGI("TIME", "🕐 Reading current time");

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    currentTime.hours = timeinfo.tm_hour;
    currentTime.minutes = timeinfo.tm_min;
    currentTime.seconds = timeinfo.tm_sec;

    if (timeinfo.tm_year > (2020 - 1900)) {
        ESP_LOGI("TIME", "✅ Time ready: %04d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        return ESP_OK;
    }

    ESP_LOGW("TIME", "⚠️ Time not yet synchronized (NTP pending)");
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

// Sprawdź czy właśnie jest 6:00
bool is_6AM_now(void)
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
            sync_time_with_ntp();  // Opcjonalnie synchronizuj czas o 6:00

            return true;
        }
    }
    return false;
}


bool is_one_hour_elapsed(void)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

        if (timeinfo.tm_min == 0 && timeinfo.tm_sec == 0) {
            return true;
        }
    return true;

}