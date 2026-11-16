#include "ch552_manager.h"

#include <string.h>

#define CH552_FRAME_MAGIC_0		0xC5U
#define CH552_FRAME_MAGIC_1		0x5AU
#define CH552_CMD_GET_IDENTITY	0x01U
#define CH552_CMD_NOP			0x00U
#define CH552_STATUS_READY		0x00U

static uint16_t ch552_crc16_ccitt(const uint8_t *data, size_t length)
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

static void ch552_build_request(uint8_t *frame, uint8_t command,
	uint8_t sequence)
{
	uint16_t crc;

	memset(frame, 0, CH552_MANAGER_REQUEST_SIZE);
	frame[0] = CH552_FRAME_MAGIC_0;
	frame[1] = CH552_FRAME_MAGIC_1;
	frame[2] = CH552_MANAGER_PROTOCOL_VERSION;
	frame[3] = command;
	frame[4] = sequence;
	crc = ch552_crc16_ccitt(frame, CH552_MANAGER_REQUEST_SIZE - 2U);
	frame[6] = (uint8_t)(crc & 0xFFU);
	frame[7] = (uint8_t)(crc >> 8);
}

static esp_err_t ch552_parse_identity(const uint8_t *frame,
	uint8_t expected_sequence, ch552_identity_t *identity)
{
	uint16_t received_crc;
	uint16_t calculated_crc;

	if ((frame[0] != CH552_FRAME_MAGIC_0) ||
		(frame[1] != CH552_FRAME_MAGIC_1) ||
		(frame[2] != CH552_MANAGER_PROTOCOL_VERSION) ||
		(frame[3] != CH552_CMD_GET_IDENTITY) ||
		(frame[4] != expected_sequence)) {
		return ESP_ERR_INVALID_RESPONSE;
	}

	received_crc = (uint16_t)frame[14] |
		((uint16_t)frame[15] << 8);
	calculated_crc = ch552_crc16_ccitt(frame,
		CH552_MANAGER_RESPONSE_SIZE - 2U);

	if (received_crc != calculated_crc) {
		return ESP_ERR_INVALID_CRC;
	}

	if (frame[5] != CH552_STATUS_READY) {
		return ESP_ERR_INVALID_STATE;
	}

	identity->protocol_version = frame[2];
	identity->status = frame[5];
	identity->major = frame[6];
	identity->minor = frame[7];
	identity->patch = frame[8];
	identity->build_id = (uint32_t)frame[9] |
		((uint32_t)frame[10] << 8) |
		((uint32_t)frame[11] << 16) |
		((uint32_t)frame[12] << 24);
	identity->capabilities = (uint16_t)frame[13];

	return ESP_OK;
}

esp_err_t ch552_manager_init(ch552_manager_t *manager,
	ch552_spi_transfer_fn transfer, void *context)
{
	if ((manager == NULL) || (transfer == NULL)) {
		return ESP_ERR_INVALID_ARG;
	}

	memset(manager, 0, sizeof(*manager));
	manager->transfer = transfer;
	manager->context = context;

	return ESP_OK;
}

esp_err_t ch552_manager_read_identity(ch552_manager_t *manager,
	ch552_identity_t *identity)
{
	uint8_t tx[CH552_MANAGER_RESPONSE_SIZE] = { 0 };
	uint8_t rx[CH552_MANAGER_RESPONSE_SIZE] = { 0 };
	uint8_t sequence;
	esp_err_t err;

	if ((manager == NULL) || (identity == NULL) ||
		(manager->transfer == NULL)) {
		return ESP_ERR_INVALID_ARG;
	}

	sequence = ++manager->sequence;
	ch552_build_request(tx, CH552_CMD_GET_IDENTITY, sequence);

	/* Transaction N delivers the request to the CH552 SPI slave. */
	err = manager->transfer(tx, rx, CH552_MANAGER_REQUEST_SIZE,
		manager->context);
	if (err != ESP_OK) {
		return err;
	}

	/* Transaction N+1 clocks out the response prepared by the slave. */
	ch552_build_request(tx, CH552_CMD_NOP, sequence);
	memset(rx, 0, sizeof(rx));
	err = manager->transfer(tx, rx, CH552_MANAGER_RESPONSE_SIZE,
		manager->context);
	if (err != ESP_OK) {
		return err;
	}

	return ch552_parse_identity(rx, sequence, identity);
}

esp_err_t ch552_manager_update(const ch552_firmware_image_t *image)
{
	if ((image == NULL) || (image->data == NULL) || (image->size == 0U)) {
		return ESP_ERR_INVALID_ARG;
	}

	/*
	 * Flashing is intentionally disabled. The reviewed UART ISP source
	 * issues an erase command whose physical coverage is not established.
	 * Do not replace this guard until the actual QT Py ROM revision,
	 * erase range and application-only write path have been verified.
	 */
	return ESP_ERR_NOT_SUPPORTED;
}
