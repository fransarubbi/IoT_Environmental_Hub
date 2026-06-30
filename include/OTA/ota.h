#ifndef OTA_H
#define OTA_H
#include <esp_err.h>

#define CURRENT_FIRMWARE_VERSION "0.8.0"

esp_err_t ota_from_github(void);
void check_update(void);


#endif //OTA_H