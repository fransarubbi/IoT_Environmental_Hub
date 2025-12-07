#define MBEDTLS_CONFIG_FILE "mbedtls/esp_config.h"
#include "mbedtls/platform.h"
#include "mbedtls/aes.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/base64.h"
#include "esp_log.h"
#include "AES-CTR/aes-ctr.h"
#include "Setting/settings.h"


static const char *TAG = "AES_CTR";



/**
 * @brief Cifra datos en AES-CTR y devuelve el resultado codificado en Base64.
 *
 * @param input Cadena de entrada (texto plano) a cifrar.
 * @param input_len Longitud de la cadena de entrada.
 * @param iv_out Buffer de salida (16 bytes) donde se genera y guarda el IV usado.
 * @param output_base64 Buffer de salida donde se almacena el texto cifrado en Base64.
 * @param output_base64_len Tamaño maximo del buffer de salida (incluye terminador '\0').
 */
uint8_t aes_ctr_encrypt_to_base64(const unsigned char *input, size_t input_len,
                               unsigned char *iv_out, char *output_base64, size_t output_base64_len) {

    size_t required_len = ((input_len + 2) / 3) * 4 + 1;
    if (output_base64_len < required_len) {
        ESP_LOGE(TAG, "- ERROR: Buffer insuficiente, necesitas %zu bytes, tienes %zu",
                 required_len, output_base64_len);
        return 0;
    }

    size_t nc_off = 0;
    unsigned char stream_block[16];
    memset(stream_block, 0, sizeof(stream_block));
    unsigned char *ciphertext = NULL;
    size_t olen = 0;
    int mbed_ret = 0;
    uint8_t ok = 0;

    ciphertext = (unsigned char*)malloc(input_len);
    if (!ciphertext) {
        ESP_LOGE(TAG, "- ERROR: No hay memoria -");
        return 0;
    }

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    const char *pers = "aes_ctr_iv";
    mbed_ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                (const unsigned char *)pers, strlen(pers));
    if (mbed_ret != 0) {
        ESP_LOGE(TAG, "- ERROR: Error inicializando RNG (-0x%04X) -", -mbed_ret);
        goto cleanup;
    }

    mbed_ret = mbedtls_ctr_drbg_random(&ctr_drbg, iv_out, IV_LEN);
    if (mbed_ret != 0) {
        ESP_LOGE(TAG, "- ERROR: Error generando IV (-0x%04X) -", -mbed_ret);
        goto cleanup;
    }

    unsigned char iv_copy[IV_LEN];
    memcpy(iv_copy, iv_out, IV_LEN);

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);

    mbed_ret = mbedtls_aes_setkey_enc(&aes, (const unsigned char*)settings.aes_key, 256);
    if (mbed_ret != 0) {
        ESP_LOGE(TAG, "- ERROR: Error configurando clave (-0x%04X) -", -mbed_ret);
        mbedtls_aes_free(&aes);
        goto cleanup;
    }

    mbed_ret = mbedtls_aes_crypt_ctr(&aes, input_len, &nc_off, iv_copy,
                                stream_block, input, ciphertext);
    if (mbed_ret != 0) {
        ESP_LOGE(TAG, "- ERROR: Error cifrando (-0x%04X) -", -mbed_ret);
        mbedtls_aes_free(&aes);
        goto cleanup;
    }

    mbed_ret = mbedtls_base64_encode((unsigned char *)output_base64, output_base64_len,
                                &olen, ciphertext, input_len);
    if (mbed_ret != 0) {
        ESP_LOGE(TAG, "- ERROR: Error base64 (-0x%04X), necesitas %zu bytes -", -mbed_ret, required_len);
        mbedtls_aes_free(&aes);
        goto cleanup;
    }

    output_base64[olen] = '\0';
    ESP_LOGI(TAG, "- OK: Cifrado exitoso: %zu bytes → %zu bytes Base64 -", input_len, olen);

    mbedtls_aes_free(&aes);
    ok = 1; // éxito

cleanup:
    if (*ciphertext) {
        mbedtls_platform_zeroize(ciphertext, input_len);
        free(ciphertext);
    }
    /* limpiar datos temporales sensibles */
    mbedtls_platform_zeroize(&stream_block, sizeof(stream_block));
    mbedtls_platform_zeroize(&iv_copy, sizeof(iv_copy));
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    return ok;
}