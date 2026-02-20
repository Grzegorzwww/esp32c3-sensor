
#include "esp_log.h"
#include "esp_system.h"
#include "esp_sntp.h"
#include "time.h"


#define MAX_TIME_READ_RETRY 10



typedef struct {
    int hours;
    int minutes;
    int seconds;
} current_time;


esp_err_t init_time();
esp_err_t get_current_time(current_time* out_time);
esp_err_t sync_time_with_ntp();
bool is_5AM_now(void);