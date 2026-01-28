#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "OTA/ota.h"
#include "esp_task_wdt.h"
#include "Fsm/fsm.h"
#include "System/system.h"


#define URL_VERSION "https://https://raw.githubusercontent.com/fransarubbi/IoT_Environmental_Hub/refs/heads/master/version.txt"
#define URL_BIN     "https://raw.githubusercontent.com/TuUsuario/Repo/main/firmware.bin"
#define MAX_HTTP_OUTPUT_BUFFER 64


static const char *TAG = "OTA";
static char response_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};


typedef struct {
    char *buffer;
    int buffer_len;
    int data_len;
} version_data_t;


/**
 * @brief Handler de eventos para el cliente HTTP.
 * * Se ejecuta automáticamente cuando el cliente HTTP recibe datos o cambia de estado.
 * Su objetivo principal es capturar el cuerpo de la respuesta (la versión) y guardarlo en el buffer.
 * * @param evt Puntero a la estructura del evento HTTP.
 * @return ESP_OK si todo sale bien.
 */
esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (!esp_http_client_is_chunked_response(evt->client)) {
            version_data_t *vdata = (version_data_t *)evt->user_data;
            // Calculamos cuánto espacio queda
            int space_left = vdata->buffer_len - vdata->data_len - 1;
            int copy_len = evt->data_len;

            if (copy_len > space_left) copy_len = space_left;

            if (copy_len > 0) {
                memcpy(vdata->buffer + vdata->data_len, evt->data, copy_len);
                vdata->data_len += copy_len;
                vdata->buffer[vdata->data_len] = '\0';
            }
        }
    }
    return ESP_OK;
}


/**
 * @brief Compara dos versiones en formato Semantic Versioning (X.Y.Z).
 * * @param new_ver Cadena con la nueva versión (ej: "1.0.1").
 * @param current_ver Cadena con la versión actual (ej: "1.0.0").
 * * @return int
 * > 0 : La nueva versión es mayor (Actualizar).
 * 0   : Las versiones son iguales.
 * < 0 : La versión actual es mayor.
 */
static int compare_semver(const char *new_ver, const char *current_ver) {
    char v1[32], v2[32];

    strncpy(v1, new_ver, sizeof(v1) - 1);
    v1[sizeof(v1) - 1] = '\0';

    strncpy(v2, current_ver, sizeof(v2) - 1);
    v2[sizeof(v2) - 1] = '\0';

    char *token1 = strtok(v1, ".");
    char *token2 = strtok(v2, ".");

    while (token1 != NULL && token2 != NULL) {
        int num1 = atoi(token1);
        int num2 = atoi(token2);
        if (num1 != num2) {
            return num1 - num2;
        }
        token1 = strtok(NULL, ".");
        token2 = strtok(NULL, ".");
    }
    return 0;
}


/**
 * @brief Realiza una petición HTTPS GET para obtener la versión del repositorio.
 * * @note Esta función no retorna error explícito al caller, guarda el resultado en response_buffer.
 */
esp_err_t get_repository_version() {
    ESP_LOGI(TAG, "INFO: Chequeando version del repo...");
    memset(response_buffer, 0, MAX_HTTP_OUTPUT_BUFFER);

    version_data_t vdata = {
        .buffer = response_buffer,
        .buffer_len = MAX_HTTP_OUTPUT_BUFFER,
        .data_len = 0
    };

    esp_http_client_config_t config = {
        .url = URL_VERSION,
        .timeout_ms = 10000,
        .event_handler = http_event_handler,
        .user_data = &vdata,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        if (esp_http_client_get_status_code(client) != 200) {
            err = ESP_FAIL;
        }
    }

    esp_http_client_cleanup(client);
    return err;
}


/**
 * @brief Ejecuta el proceso de actualización OTA (Over The Air).
 * * Descarga el binario, lo escribe en la partición OTA libre y reinicia el ESP32.
 * * @return esp_err_t ESP_OK si tuvo éxito (el sistema se reiniciará antes de retornar), o código de error.
 */
esp_err_t ota_from_github() {
    ESP_LOGI(TAG, "INFO: Iniciando descarga de Firmware...");

    esp_http_client_config_t config = {
        .url = URL_BIN,
        .timeout_ms = 30000,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
        .partial_http_download = true,
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ERROR: Fallo al iniciar OTA: %s", esp_err_to_name(err));
        return err;
    }

    while (1) {
        err = esp_https_ota_perform(https_ota_handle);  // Descarga un fragmento y lo escribe en flash
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        esp_task_wdt_reset();
    }

    // Validar imagen
    esp_err_t ota_finish_err = esp_https_ota_finish(https_ota_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ERROR: Descarga interrumpida: %s", esp_err_to_name(err));
        return err;
    }
    if (ota_finish_err != ESP_OK) {
        ESP_LOGE(TAG, "ERROR: Validación final fallida: %s", esp_err_to_name(ota_finish_err));
        return ota_finish_err;
    }

    ESP_LOGI(TAG, "INFO: OTA Completo y Validado");
    return ESP_OK;
}


/**
 * @brief Verifica la versión y notifica si existe una actualización disponible.
 */
void check_update(void) {
    if (get_repository_version() == ESP_OK) {
        if (compare_semver(response_buffer, CURRENT_FIRMWARE_VERSION) > 0) {
            ESP_LOGW(TAG, "WARNING: Actualizacion encontrada");
            uint32_t flag = UPDATE_FLAG;
            xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
        }
        else {
            ESP_LOGI(TAG, "INFO: El sistema esta actualizado.");
            uint32_t flag = 0;
            xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
        }
    }
    else {
        ESP_LOGE(TAG, "ERROR: No se pudo verificar la version");
        uint32_t flag = 0;
        xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
    }
}

