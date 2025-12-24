#include "Time/time.h"
#include <sys/time.h>
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_event.h"
#include "lwip/sys.h"


static const char *TAG = "Time";


/**
 * @brief Esperar sincronizacion de la hora local
 * @return esp_err_t Devuelve ESP_OK cuando la sincronizacion fue correcta y ESP_FAIL si falla.
 */
static esp_err_t wait_for_time_sync(void) {
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 15;

    while (timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count) {
        ESP_LOGI(TAG, "- INFO: Esperando sincronizacion SNTP... (%d/%d) -", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(TIME_WAIT));
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    if (retry == retry_count) {
        return ESP_FAIL;
    }
    return ESP_OK;
}


/**
 * @brief Inicializa la fecha y hora local
 * @return esp_err_t Devuelve ESP_OK cuando el proceso fue correcto y ESP_FAIL cuando falla.
 */
esp_err_t time_init(void) {
    ESP_LOGI(TAG, "- INFO: Inicializando SNTP -");
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    esp_err_t ret = wait_for_time_sync();
    if (ret != ESP_OK) return ESP_FAIL;
    setenv("TZ", "GMT+3", 1);  // Zona horaria de Argentina
    tzset();
    return ESP_OK;
}


/**
 * @brief Obtener la fecha y hora actual.
 * @param time_str String que almacenara la fecha y hora.
 */
void get_time(char *time_str) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(time_str, TIME_MAX_LEN, "%d/%m/%Y %H:%M:%S", &timeinfo);
}





