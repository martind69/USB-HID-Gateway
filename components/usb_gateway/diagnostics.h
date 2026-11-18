#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "usb_gateway.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DIAGNOSTICS_MAX_HOST_DEVICES 8
#define DIAGNOSTICS_NAME_LENGTH 32

typedef enum {
	DIAG_STATE_UNKNOWN = 0,
	DIAG_STATE_NOT_INITIALIZED,
	DIAG_STATE_READY,
	DIAG_STATE_CONNECTED,
	DIAG_STATE_ERROR,
	DIAG_STATE_UNAVAILABLE
} diagnostics_state_t;

typedef struct {
	bool present;
	uint8_t address;
	uint16_t vid;
	uint16_t pid;
	uint8_t interface_count;
	uint8_t hid_interface_count;
	uint16_t report_descriptor_length;
	char manufacturer[DIAGNOSTICS_NAME_LENGTH];
	char product[DIAGNOSTICS_NAME_LENGTH];
} diagnostics_host_device_t;

void diagnostics_init(void);
void diagnostics_process(usb_gateway_mode_t mode);

void diagnostics_set_host_state(diagnostics_state_t state, int error_code);
void diagnostics_set_device_state(diagnostics_state_t state, int error_code);
void diagnostics_set_proxy_state(diagnostics_state_t state, int error_code);
void diagnostics_set_i2c_state(uint8_t bus, uint8_t address,
	diagnostics_state_t state, int error_code);

void diagnostics_host_device_added(const diagnostics_host_device_t *device);
void diagnostics_host_device_removed(uint8_t address);

void diagnostics_report_hid_enum(uint8_t address, uint8_t interface_number,
	uint8_t report_id, uint16_t report_length);
void diagnostics_report_proxy_forwarded(void);
void diagnostics_report_proxy_processed(void);
void diagnostics_report_proxy_error(int error_code);
void diagnostics_report_hid_error(int error_code);
void diagnostics_report_i2c_error(uint8_t bus, uint8_t address,
	int error_code);

#ifdef __cplusplus
}
#endif
