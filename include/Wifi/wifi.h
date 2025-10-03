#ifndef WIFI_H
#define WIFI_H

#define WIFI_MAX_RETRY     5
#define WIFI_TIMEOUT       10000
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#include "esp_err.h"

esp_err_t wifi_init(void);


#endif //WIFI_H