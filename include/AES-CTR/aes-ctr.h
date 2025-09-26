#ifndef AES_CTR_H
#define AES_CTR_H

#define IV_LEN 16   // Initialization Vector

#include <string.h>


void aes_ctr_encrypt_to_base64(const unsigned char *input, size_t input_len,
                                      unsigned char *iv_out, char *output_base64, size_t output_base64_len);


#endif //AES_CTR_H