#pragma once
#include "esp_err.h"
int log_stub(const char*,...) __attribute__((format(printf,1,2)));
#define ESP_LOGI(t,...) do{(void)(t);log_stub(__VA_ARGS__);}while(0)
#define ESP_LOGW ESP_LOGI
#define ESP_LOGE ESP_LOGI
#define ESP_LOGD ESP_LOGI
