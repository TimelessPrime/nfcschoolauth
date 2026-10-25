#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t dns_server_start(const char *ip_address);
void dns_server_stop(void);

#ifdef __cplusplus
}
#endif
