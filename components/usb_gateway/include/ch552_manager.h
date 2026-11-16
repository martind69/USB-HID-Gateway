#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CH552_MANAGER_PROTOCOL_VERSION	1U
#define CH552_MANAGER_REQUEST_SIZE		8U
#define CH552_MANAGER_RESPONSE_SIZE		16U

typedef struct {
	uint8_t major;
	uint8_t minor;
	uint8_t patch;
	uint32_t build_id;
	uint16_t capabilities;
	uint8_t protocol_version;
	uint8_t status;
} ch552_identity_t;

/*
 * SPI transport contract:
 * - One callback invocation transfers exactly tx_size bytes and receives
 *   exactly rx_size bytes under one CS assertion.
 * - The CH552 slave consumes the request during transaction N.
 * - Its response is returned during transaction N+1.
 * - The callback must not change GPIO ownership or perform UART operations.
 */
typedef esp_err_t (*ch552_spi_transfer_fn)(
	const uint8_t *tx,
	uint8_t *rx,
	size_t length,
	void *context
);

typedef struct {
	ch552_spi_transfer_fn transfer;
	void *context;
	uint8_t sequence;
} ch552_manager_t;

typedef struct {
	uint8_t major;
	uint8_t minor;
	uint8_t patch;
	uint32_t build_id;
	const uint8_t *data;
	size_t size;
	uint32_t crc32;
} ch552_firmware_image_t;

typedef enum {
	CH552_MANAGER_STATUS_UNKNOWN = 0,
	CH552_MANAGER_STATUS_RESPONSIVE,
	CH552_MANAGER_STATUS_VERSION_MISMATCH,
	CH552_MANAGER_STATUS_UPDATE_BLOCKED,
	CH552_MANAGER_STATUS_ERROR
} ch552_manager_status_t;

esp_err_t ch552_manager_init(ch552_manager_t *manager,
	ch552_spi_transfer_fn transfer, void *context);

esp_err_t ch552_manager_read_identity(ch552_manager_t *manager,
	ch552_identity_t *identity);

/*
 * Deliberately fails closed until the ROM ISP erase scope and write protocol
 * are proven to preserve 0x3FF8-0x3FFF on the actual QT Py bootloader.
 * This function performs no UART activity and no flash operation.
 */
esp_err_t ch552_manager_update(const ch552_firmware_image_t *image);

#ifdef __cplusplus
}
#endif
