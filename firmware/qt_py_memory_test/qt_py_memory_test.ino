/*
 * CH552 QT Py memory-sizing application.
 *
 * Target: Adafruit QT Py CH552
 * Toolchain: DeqingSun ch55xduino / SDCC
 * Board options: CH552, internal 16 MHz, 3.3 V, P3.6 boot pin,
 *                USB RAM setting "user148"
 *
 * This is a resource baseline, not the final Logitech G815 device firmware.
 * The target is the G815 composite HID device (keyboard + vendor HID).
 * The current USBHIDKeyboard stack remains a temporary single-interface stand-in.
 * See docs/CH552_G815_USB_PROFILE.md for descriptor-derived target requirements.
 */

#ifndef USER_USB_RAM
#error "Select ch55xduino USB setting user148"
#endif

#include <stdint.h>
#include "src/userUsbHidKeyboard/USBHIDKeyboard.h"

/* CH552 SFR declarations are provided by the ch55xduino core. */

#define SPI_IRQ_ENABLE		0x01U
#define SPI_MODE_SLAVE		0x80U
#define SPI_IRQ_FIRST		0x20U
#define SPI_IRQ_BYTE		0x10U
#define SPI_MISO_ENABLE		0x80U

#define FRAME_MAGIC_0		0xC5U
#define FRAME_MAGIC_1		0x5AU
#define PROTOCOL_VERSION	1U
#define CMD_GET_IDENTITY	0x01U
#define STATUS_READY		0x00U
#define STATUS_BAD_FRAME	0x01U

#define REQUEST_LENGTH		8U
#define REQUEST_MAX_LENGTH	72U
#define RESPONSE_LENGTH		17U
#define G815_KEYBOARD_REPORT_MAX	8U
#define G815_VENDOR_REPORT_MAX	64U

#define FW_VERSION_MAJOR	0U
#define FW_VERSION_MINOR	1U
#define FW_VERSION_PATCH	0U
#define FW_BUILD_ID			0x20261116UL

/*
 * Descriptor-derived target inventory for the CH552's assigned G815 role.
 * These values document the required profile; the temporary USBHIDKeyboard
 * library below does not emit this composite Logitech configuration yet.
 */
#define G815_INTERFACE_COUNT			2U
#define G815_KEYBOARD_INTERFACE			0U
#define G815_VENDOR_INTERFACE			1U
#define G815_KEYBOARD_EP_IN				0x81U
#define G815_VENDOR_EP_IN				0x82U
#define G815_KEYBOARD_EP_MPS			8U
#define G815_VENDOR_EP_MPS			64U
#define G815_INTERRUPT_INTERVAL		1U
#define G815_KEYBOARD_REPORT_DESC_LEN	65U
#define G815_VENDOR_REPORT_DESC_LEN	188U


/*
 * USB endpoint DMA memory is reserved separately by the ch55xduino
 * user148 profile. These xRAM buffers model the P4-to-CH552 report path:
 * maximum SPI request, identity response, keyboard report and vendor report.
 * The request buffer is sized for a 6-byte header, 64-byte payload and CRC.
 */
static __xdata uint8_t request_frame[REQUEST_MAX_LENGTH];
static __xdata uint8_t response_frame[RESPONSE_LENGTH];
static __xdata volatile uint8_t keyboard_report_buffer[G815_KEYBOARD_REPORT_MAX];
static __xdata volatile uint8_t vendor_report_buffer[G815_VENDOR_REPORT_MAX];
static __data volatile uint8_t memory_probe_sink;
static __data uint8_t spi_index;
static __data uint8_t spi_phase;

/* CRC-16/CCITT-FALSE: polynomial 0x1021, init 0xFFFF, no reflection. */
#pragma NOOVERLAY
static uint16_t crc16_ccitt(const __xdata uint8_t *data, uint8_t length)
{
	uint16_t crc = 0xFFFFU;
	uint8_t i;
	uint8_t bit;

	for (i = 0U; i < length; i++) {
		crc ^= (uint16_t)data[i] << 8;

		for (bit = 0U; bit < 8U; bit++) {
			if ((crc & 0x8000U) != 0U) {
				crc = (uint16_t)((crc << 1) ^ 0x1021U);
			} else {
				crc <<= 1;
			}
		}
	}

	return crc;
}

#pragma NOOVERLAY
static void build_identity_response(void)
{
	uint16_t crc;
	uint8_t valid = 1U;

	if ((request_frame[0] != FRAME_MAGIC_0) ||
		(request_frame[1] != FRAME_MAGIC_1) ||
		(request_frame[2] != PROTOCOL_VERSION) ||
		(request_frame[3] != CMD_GET_IDENTITY)) {
		valid = 0U;
	}

	crc = crc16_ccitt(request_frame, REQUEST_LENGTH - 2U);
	if ((request_frame[6] != (uint8_t)crc) ||
		(request_frame[7] != (uint8_t)(crc >> 8))) {
		valid = 0U;
	}

	response_frame[0] = FRAME_MAGIC_0;
	response_frame[1] = FRAME_MAGIC_1;
	response_frame[2] = PROTOCOL_VERSION;
	response_frame[3] = CMD_GET_IDENTITY;
	response_frame[4] = request_frame[4];
	response_frame[5] = valid ? STATUS_READY : STATUS_BAD_FRAME;
	response_frame[6] = FW_VERSION_MAJOR;
	response_frame[7] = FW_VERSION_MINOR;
	response_frame[8] = FW_VERSION_PATCH;
	response_frame[9] = (uint8_t)(FW_BUILD_ID);
	response_frame[10] = (uint8_t)(FW_BUILD_ID >> 8);
	response_frame[11] = (uint8_t)(FW_BUILD_ID >> 16);
	response_frame[12] = (uint8_t)(FW_BUILD_ID >> 24);
	response_frame[13] = 0x03U;	/* HID + SPI management */
	response_frame[14] = 0x00U;

	crc = crc16_ccitt(response_frame, RESPONSE_LENGTH - 2U);
	response_frame[15] = (uint8_t)crc;
	response_frame[16] = (uint8_t)(crc >> 8);
}

/*
 * SPI0 slave interrupt.
 * The P4 sends the 8-byte request in one CS transaction. The CH552 prepares
 * the 17-byte response and returns it in the following CS transaction.
 */
void spi0_isr(void) __interrupt (6)
{
	uint8_t received;

	if (S0_IF_FIRST != 0) {
		S0_IF_FIRST = 0;
		spi_index = 0U;
	}

	if (S0_IF_BYTE != 0) {
		S0_IF_BYTE = 0;
		received = SPI0_DATA;

		if (spi_phase == 0U) {
			if (spi_index < REQUEST_LENGTH) {
				request_frame[spi_index] = received;
				spi_index++;

				if (spi_index == REQUEST_LENGTH) {
					build_identity_response();
					spi_phase = 1U;
					SPI0_S_PRE = response_frame[0];
				} else {
					SPI0_DATA = 0xFFU;
				}
			}
		} else {
			spi_index++;

			if (spi_index < RESPONSE_LENGTH) {
				SPI0_DATA = response_frame[spi_index];
			} else {
				spi_phase = 0U;
				SPI0_S_PRE = 0xFFU;
			}
		}
	}
}

/*
 * Application-assisted UART ISP handoff.
 * P3.2/INT0 is edge-triggered through the external coupling network.
 */
void boot_req_isr(void) __interrupt (0)
{
	USB_CTRL = 0U;
	EA = 0U;

	/* Allow the USB peripheral and interrupt activity to stop cleanly. */
	delayMicroseconds(50000U);
	delayMicroseconds(50000U);

	__asm__ ("lcall #0x3800");

	while (1) {
	}
}

static void spi_slave_init(void)
{
	/* P1.4=SCS, P1.5=MOSI, P1.6=MISO and P1.7=SCK are inputs. */
	P1_MOD_OC &= (uint8_t)~0xF0U;
	P1_DIR_PU &= (uint8_t)~0xF0U;

	/* Configure MISO as high-impedance when the slave is not selected. */
	P1_MOD_OC &= (uint8_t)~0x40U;
	P1_DIR_PU &= (uint8_t)~0x40U;

	SPI0_CTRL = SPI_MISO_ENABLE;
	SPI0_S_PRE = 0xFFU;
	SPI0_SETUP = SPI_MODE_SLAVE | SPI_IRQ_FIRST | SPI_IRQ_BYTE;
	IE_EX |= SPI_IRQ_ENABLE;
}

static void boot_req_init(void)
{
	/* P3.2/INT0 is an input; provide the idle bias on the board. */
	P3_MOD_OC &= (uint8_t)~0x04U;
	P3_DIR_PU &= (uint8_t)~0x04U;

	IT0 = 1U;
	EX0 = 1U;
}

void setup(void)
{
	spi_index = 0U;
	spi_phase = 0U;

	USBInit();
	spi_slave_init();
	boot_req_init();

	EA = 1U;
}

void loop(void)
{
	/* Keep the target report staging buffers present for the memory probe. */
	memory_probe_sink = keyboard_report_buffer[0] ^ vendor_report_buffer[0];

	/* USB servicing is interrupt-driven by the ch55xduino HID stack. */
}
