#include "dac_output.h"

#include "diagnostics.h"
#include "esp_log.h"

static const char *TAG = "dac_output";

void dac_output_init(void)
{
	/* DAC7578 uses I2C; the physical bus driver is not implemented yet. */
	diagnostics_set_i2c_state(0, 0x4C, DIAG_STATE_NOT_INITIALIZED, 0);
	ESP_LOGW(TAG, "DAC7578 I2C driver not implemented");
}

void dac_output_set_channel(uint8_t channel, uint16_t value)
{
	/* TODO: Validate channel range and queue an I2C DAC update. */
	(void)channel;
	(void)value;
}
