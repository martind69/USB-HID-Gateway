#include "spi_link.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/spi_slave.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define LINK_SPI_HOST		SPI2_HOST
#define LINK_PIN_SCLK		6
#define LINK_PIN_MOSI		7
#define LINK_PIN_MISO		8
#define LINK_PIN_CS		9
#define LINK_FRAME_SIZE		72U
#define LINK_PAYLOAD_SIZE	64U
#define LINK_PROTOCOL		1U
#define LINK_MAGIC_0		0xC5U
#define LINK_MAGIC_1		0x5AU
#define LINK_MSG_STATUS		0x80U

static const char *TAG = "spi_link";

static DMA_ATTR uint8_t s_rx_frame[LINK_FRAME_SIZE] __attribute__((aligned(4)));
static DMA_ATTR uint8_t s_tx_frame[LINK_FRAME_SIZE] __attribute__((aligned(4)));

static uint16_t link_crc16(const uint8_t *data, size_t length)
{
	uint16_t crc = 0xFFFFU;

	for (size_t i = 0; i < length; i++) {
		crc ^= (uint16_t)data[i] << 8;
		for (uint8_t bit = 0; bit < 8U; bit++) {
			if ((crc & 0x8000U) != 0U) {
				crc = (uint16_t)((crc << 1) ^ 0x1021U);
			} else {
				crc <<= 1;
			}
		}
	}

	return crc;
}

static bool link_frame_valid(const uint8_t frame[LINK_FRAME_SIZE])
{
	uint16_t received_crc;
	uint16_t calculated_crc;

	if (frame[0] != LINK_MAGIC_0 || frame[1] != LINK_MAGIC_1 ||
		frame[2] != LINK_PROTOCOL || frame[5] > LINK_PAYLOAD_SIZE) {
		return false;
	}

	received_crc = (uint16_t)frame[70] |
		((uint16_t)frame[71] << 8);
	calculated_crc = link_crc16(frame, 70U);

	return received_crc == calculated_crc;
}

static void link_prepare_status(uint8_t sequence, uint8_t status)
{
	uint16_t crc;

	memset(s_tx_frame, 0, sizeof(s_tx_frame));
	s_tx_frame[0] = LINK_MAGIC_0;
	s_tx_frame[1] = LINK_MAGIC_1;
	s_tx_frame[2] = LINK_PROTOCOL;
	s_tx_frame[3] = LINK_MSG_STATUS;
	s_tx_frame[4] = sequence;
	s_tx_frame[5] = 1U;
	s_tx_frame[6] = status;
	crc = link_crc16(s_tx_frame, 70U);
	s_tx_frame[70] = (uint8_t)(crc & 0xFFU);
	s_tx_frame[71] = (uint8_t)(crc >> 8);
}

esp_err_t spi_link_init(void)
{
	spi_bus_config_t bus_config = {
		.mosi_io_num = LINK_PIN_MOSI,
		.miso_io_num = LINK_PIN_MISO,
		.sclk_io_num = LINK_PIN_SCLK,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = LINK_FRAME_SIZE
	};
	spi_slave_interface_config_t slave_config = {
		.spics_io_num = LINK_PIN_CS,
		.flags = 0,
		.queue_size = 1,
		.mode = 0
	};
	esp_err_t result;

	link_prepare_status(0U, 0U);
	result = spi_slave_initialize(LINK_SPI_HOST, &bus_config,
		&slave_config, SPI_DMA_CH_AUTO);
	if (result != ESP_OK) {
		ESP_LOGE(TAG, "SPI slave initialization failed: %s",
			esp_err_to_name(result));
		return result;
	}

	ESP_LOGI(TAG, "SPI slave ready on SCLK=%d MOSI=%d MISO=%d CS=%d",
		LINK_PIN_SCLK, LINK_PIN_MOSI, LINK_PIN_MISO, LINK_PIN_CS);
	ESP_LOGW(TAG, "SPI GPIO assignment is provisional; verify exact board revision");
	return ESP_OK;
}

void spi_link_task(void)
{
	spi_slave_transaction_t transaction = {
		.length = LINK_FRAME_SIZE * 8U,
		.tx_buffer = s_tx_frame,
		.rx_buffer = s_rx_frame
	};

	while (true) {
		esp_err_t result = spi_slave_transmit(LINK_SPI_HOST,
			&transaction, portMAX_DELAY);
		if (result != ESP_OK) {
			ESP_LOGE(TAG, "SPI slave transaction failed: %s",
				esp_err_to_name(result));
			continue;
		}

		if (!link_frame_valid(s_rx_frame)) {
			ESP_LOGW(TAG, "Rejected invalid SPI frame");
			link_prepare_status(s_rx_frame[4], 1U);
			continue;
		}

		/*
		 * The complete command dispatcher and report forwarding are added
		 * with the P4 master implementation. The current response is ACK-only.
		 */
		link_prepare_status(s_rx_frame[4], 0U);
	}
}
