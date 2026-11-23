#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t spi_link_init(void);
void spi_link_task(void *argument);

#ifdef __cplusplus
}
#endif
