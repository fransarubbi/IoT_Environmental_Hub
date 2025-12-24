#ifndef TIME_H
#define TIME_H

#define TIME_WAIT 2000
#define TIME_MAX_LEN 30

#include "esp_err.h"

esp_err_t time_init(void);
void get_time(char *time_str);

#endif