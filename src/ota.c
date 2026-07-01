#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "OTA/ota.h"
#include "esp_task_wdt.h"
#include "Fsm/fsm.h"
#include "System/system.h"


#define URL_VERSION      "https://raw.githubusercontent.com/fransarubbi/IoT_Environmental_Hub/master/version.txt"
#define URL_BIN_TEMPLATE "https://github.com/fransarubbi/IoT_Environmental_Hub/releases/download/v%s/firmware.bin"
#define MAX_HTTP_OUTPUT_BUFFER 64
#define MAX_VERSION_LEN 32
#define MAX_URL_LEN 256


static const char *TAG = "OTA";
static char response_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};

// Guarda la última versión remota (saneada) detectada por check_update().
// ota_from_github() la usa para armar dinámicamente la URL del binario.
static char latest_remote_version[MAX_VERSION_LEN] = {0};


typedef struct {
    char *buffer;
    int buffer_len;
    int data_len;
} version_data_t;


/**
 * @brief Función para eliminar saltos de linea y espacios de un string.
 * @param str String al que se le aplicará la limpieza.
 */
static void sanitize_string(char *str) {
    char *p = str;
    while (*p) {
        if (*p == '\r' || *p == '\n' || *p == ' ') {
            *p = '\0';
            break;
        }
        p++;
    }
}


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
            const int space_left = vdata->buffer_len - vdata->data_len - 1;
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
 * @brief Compara versiones semánticas.
 * Retorna 1 si remote > local (Actualizar).
 * Retorna -1 si remote < local.
 * Retorna 0 si son iguales.
 */
static int compare_semver(const char *remote_version, const char *local_version) {
    char remote[32], local[32];
    char *saveptr_rem = NULL;
    char *saveptr_loc = NULL;

    strncpy(remote, remote_version, sizeof(remote) - 1);
    remote[sizeof(remote) - 1] = '\0';
    strncpy(local, local_version, sizeof(local) - 1);
    local[sizeof(local) - 1] = '\0';

    sanitize_string(remote);
    sanitize_string(local);

    ESP_LOGI(TAG, "Info: comparando: Remoto='%s' vs Local='%s'", remote, local);

    char *token_rem = strtok_r(remote, ".", &saveptr_rem);
    char *token_loc = strtok_r(local, ".", &saveptr_loc);

    while (token_rem != NULL && token_loc != NULL) {
        const int num_rem = atoi(token_rem);
        const int num_loc = atoi(token_loc);

        if (num_rem > num_loc) return 1;
        if (num_rem < num_loc) return -1;

        token_rem = strtok_r(NULL, ".", &saveptr_rem);
        token_loc = strtok_r(NULL, ".", &saveptr_loc);
    }

    if (token_rem != NULL) return 1;
    if (token_loc != NULL) return -1;

    return 0;
}


/**
 * @brief Realiza una petición HTTPS GET para obtener la versión del repositorio.
 * * @note Esta función no retorna error explícito al caller, guarda el resultado en response_buffer.
 */
esp_err_t get_repository_version() {
    ESP_LOGI(TAG, "Info: chequeando version del repo...");
    memset(response_buffer, 0, MAX_HTTP_OUTPUT_BUFFER);

    version_data_t vdata = {
        .buffer = response_buffer,
        .buffer_len = MAX_HTTP_OUTPUT_BUFFER,
        .data_len = 0
    };

    const esp_http_client_config_t config = {
        .url = URL_VERSION,
        .timeout_ms = 10000,
        .event_handler = http_event_handler,
        .user_data = &vdata,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return ESP_FAIL;

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        if (esp_http_client_get_status_code(client) != 200) {
            ESP_LOGW(TAG, "Warning: HTTP Status %d", esp_http_client_get_status_code(client));
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "Error: fallo HTTP %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return err;
}


/**
 * @brief Ejecuta el proceso de actualización OTA (Over The Air).
 * * Arma dinámicamente la URL del binario a partir de la última versión remota
 * detectada por check_update() y descarga el firmware correspondiente,
 * lo escribe en la partición OTA libre y reinicia el ESP32.
 * * @return esp_err_t ESP_OK si tuvo éxito (el sistema se reiniciará antes de retornar),
 * o código de error (incluye ESP_ERR_INVALID_STATE si no hay una versión remota
 * conocida con la cual armar la URL).
 */
esp_err_t ota_from_github() {

    if (latest_remote_version[0] == '\0') {
        ESP_LOGE(TAG, "Error: no hay version remota conocida para armar la URL del binario");
        return ESP_ERR_INVALID_STATE;
    }

    char ota_url[MAX_URL_LEN] = {0};
    const int written = snprintf(ota_url, sizeof(ota_url), URL_BIN_TEMPLATE, latest_remote_version);
    if (written < 0 || written >= (int)sizeof(ota_url)) {
        ESP_LOGE(TAG, "Error: URL de OTA demasiado larga o invalida");
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "Info: iniciando descarga de firmware desde %s", ota_url);

    esp_http_client_config_t config = {
        .url = ota_url,
        .timeout_ms = 30000,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
    };

    const esp_https_ota_config_t ota_config = {
        .http_config = &config,
        .partial_http_download = true,
    };
    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error: OTA %s", esp_err_to_name(err));
        return err;
    }

    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error: OTA descarga %s", esp_err_to_name(err));
        esp_https_ota_finish(https_ota_handle); // Limpiar
        return err;
    }

    esp_err_t ota_finish_err = esp_https_ota_finish(https_ota_handle);
    if (ota_finish_err != ESP_OK) {
        ESP_LOGE(TAG, "Error: OTA final %s", esp_err_to_name(ota_finish_err));
        return ota_finish_err;
    }

    ESP_LOGI(TAG, "Info: OTA completo y validado");
    return ESP_OK;
}


/**
 * @brief Verifica la versión y notifica si existe una actualización disponible.
 */
void check_update(void) {
    if (get_repository_version() == ESP_OK) {

        const int result = compare_semver(response_buffer, CURRENT_FIRMWARE_VERSION);

        if (result == 1) {   // Remoto > Local
            strncpy(latest_remote_version, response_buffer, sizeof(latest_remote_version) - 1);
            latest_remote_version[sizeof(latest_remote_version) - 1] = '\0';
            sanitize_string(latest_remote_version);

            ESP_LOGW(TAG, "Warning: actualizacion encontrada (v%s). Iniciando...", latest_remote_version);
            const uint32_t flag = UPDATE_FLAG;
            xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
        }
        if (result <= 0) {
            ESP_LOGI(TAG, "Info: sistema actualizado (Remoto <= Local). No se requiere OTA");
            const uint32_t flag = 0;    // Salir de estado CHECK_FIRMWARE
            xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
        }
    } else {
        ESP_LOGE(TAG, "Error: no se pudo verificar la version");
        const uint32_t flag = 0;
        xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
    }
}