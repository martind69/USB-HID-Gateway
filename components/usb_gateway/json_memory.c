#include "json_memory.h"

#include <stdlib.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "json_memory";

static void *json_malloc(size_t size)
{
    void *memory = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return memory != NULL ? memory : malloc(size);
}

static void json_free(void *memory)
{
    /* heap_caps_free() releases internal and external blocks alike. */
    heap_caps_free(memory);
}

esp_err_t json_memory_init(void)
{
    const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    cJSON_Hooks hooks = {
        .malloc_fn = json_malloc,
        .free_fn = json_free,
    };
    cJSON_InitHooks(&hooks);
    if (psram_free == 0U) {
        ESP_LOGW(TAG, "No PSRAM available: cJSON uses internal RAM (large profiles will fail)");
        return ESP_ERR_NOT_SUPPORTED;
    }
    ESP_LOGI(TAG, "cJSON allocations in PSRAM, free=%u bytes", (unsigned)psram_free);
    return ESP_OK;
}
