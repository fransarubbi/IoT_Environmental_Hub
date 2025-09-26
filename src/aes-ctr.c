#define MBEDTLS_CONFIG_FILE "mbedtls/esp_config.h"
#include "AES-CTR/aes-ctr.h"
#include "Setting/settings.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "mbedtls/platform.h"
#include "mbedtls/aes.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/base64.h"



static const char *TAG = "AES_CTR";



/**
 * @brief Cifra datos en AES-CTR y devuelve el resultado codificado en Base64.
 *
 * @param input Cadena de entrada (texto plano) a cifrar.
 * @param input_len Longitud de la cadena de entrada.
 * @param iv_out Buffer de salida (16 bytes) donde se genera y guarda el IV usado.
 * @param output_base64 Buffer de salida donde se almacena el texto cifrado en Base64.
 * @param output_base64_len Tamaño máximo del buffer de salida (incluye terminador '\0').
 */

void aes_ctr_encrypt_to_base64(const unsigned char *input, size_t input_len,
                               unsigned char *iv_out, char *output_base64, size_t output_base64_len) {
    size_t nc_off = 0; // offset del keystream
    unsigned char stream_block[16];
    unsigned char ciphertext[256];
    size_t ciphertext_len = 0;
    size_t olen;

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    const char *pers = "aes_ctr_iv";
    int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                (const unsigned char *)pers, strlen(pers));
    if (ret != 0) {
        ESP_LOGE(TAG, "Error inicializando RNG (%d)", ret);
        return;
    }

    // Se genera de forma aleatoria el IV
    mbedtls_ctr_drbg_random(&ctr_drbg, iv_out, IV_LEN);

    // Inicializar AES-CTR
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);

    // Se carga la clave secreta compartida (aes_key). 128 indica que se usa AES-128 (16 bytes de clave).
    mbedtls_aes_setkey_enc(&aes, (unsigned char*)settings.aes_key, 256); // AES-128

    // AES-CTR genera un keystream usando (clave + IV) y lo combina con el mensaje haciendo un XOR. Como resultado, sale el ciphertext
    ret = mbedtls_aes_crypt_ctr(&aes, input_len, &nc_off, iv_out, stream_block, input, ciphertext);
    if (ret != 0) {
        ESP_LOGE(TAG, "Error cifrando (%d)", ret);
        return;
    }
    ciphertext_len = input_len;

    // Convierte los bytes cifrados en una cadena de texto ASCII para que se pueda enviar por MQTT
    ret = mbedtls_base64_encode((unsigned char *)output_base64, output_base64_len,
                                &olen, ciphertext, ciphertext_len);
    if (ret != 0) {
        ESP_LOGE(TAG, "Error base64 (%d)", ret);
        return;
    }

    output_base64[olen] = '\0';

    // Liberar
    mbedtls_aes_free(&aes);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
}