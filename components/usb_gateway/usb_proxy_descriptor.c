#include "usb_proxy_descriptor.h"

#include <string.h>

#define USB_DESC_TYPE_CONFIGURATION 0x02U
#define USB_DESC_TYPE_INTERFACE 0x04U
#define USB_DESC_TYPE_ENDPOINT 0x05U
#define USB_DESC_TYPE_HID 0x21U
#define USB_DESC_TYPE_HID_REPORT 0x22U
#define USB_DESC_TYPE_INTERFACE_ASSOCIATION 0x0BU
#define USB_DESC_MIN_CONFIGURATION_LENGTH 9U
#define USB_DESC_MIN_INTERFACE_LENGTH 9U
#define USB_DESC_MIN_ENDPOINT_LENGTH 7U
#define USB_DESC_MIN_HID_LENGTH 6U

static uint16_t read_le16(const uint8_t *data)
{
	return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static usb_proxy_interface_t *find_interface_mutable(
	usb_proxy_configuration_t *configuration,
	uint8_t interface_number,
	uint8_t alternate_setting)
{
	size_t i;

	for (i = 0U; i < configuration->interface_count; ++i) {
		usb_proxy_interface_t *interface = &configuration->interfaces[i];

		if (interface->interface_number == interface_number &&
			interface->alternate_setting == alternate_setting) {
			return interface;
		}
	}
	return NULL;
}

const usb_proxy_interface_t *usb_proxy_configuration_find_interface(
	const usb_proxy_configuration_t *configuration,
	uint8_t interface_number,
	uint8_t alternate_setting)
{
	size_t i;

	if (configuration == NULL) {
		return NULL;
	}

	for (i = 0U; i < configuration->interface_count; ++i) {
		const usb_proxy_interface_t *interface = &configuration->interfaces[i];

		if (interface->interface_number == interface_number &&
			interface->alternate_setting == alternate_setting) {
			return interface;
		}
	}
	return NULL;
}

const usb_proxy_endpoint_t *usb_proxy_configuration_find_endpoint(
	const usb_proxy_configuration_t *configuration,
	uint8_t endpoint_address,
	uint8_t interface_number,
	uint8_t alternate_setting)
{
	size_t i;

	if (configuration == NULL) {
		return NULL;
	}

	for (i = 0U; i < configuration->endpoint_count; ++i) {
		const usb_proxy_endpoint_t *endpoint = &configuration->endpoints[i];

		if (endpoint->address == endpoint_address &&
			endpoint->interface_number == interface_number &&
			endpoint->alternate_setting == alternate_setting) {
			return endpoint;
		}
	}
	return NULL;
}

usb_proxy_descriptor_result_t usb_proxy_configuration_parse(
	usb_proxy_configuration_t *configuration,
	const uint8_t *descriptor,
	size_t descriptor_length)
{
	size_t offset;
	usb_proxy_interface_t *current_interface = NULL;

	if (configuration == NULL || descriptor == NULL) {
		return USB_PROXY_DESC_ERR_ARGUMENT;
	}

	memset(configuration, 0, sizeof(*configuration));

	if (descriptor_length < USB_DESC_MIN_CONFIGURATION_LENGTH ||
		descriptor_length > USB_PROXY_DESCRIPTOR_MAX_CONFIG_SIZE) {
		return USB_PROXY_DESC_ERR_LENGTH;
	}

	if (descriptor[0] < USB_DESC_MIN_CONFIGURATION_LENGTH ||
		descriptor[1] != USB_DESC_TYPE_CONFIGURATION) {
		return USB_PROXY_DESC_ERR_TYPE;
	}

	if (read_le16(&descriptor[2]) != descriptor_length) {
		return USB_PROXY_DESC_ERR_LENGTH;
	}

	configuration->total_length = (uint16_t)descriptor_length;
	configuration->declared_interface_count = descriptor[4];
	configuration->configuration_value = descriptor[5];
	configuration->attributes = descriptor[7];
	configuration->max_power = descriptor[8];
	memcpy(configuration->raw, descriptor, descriptor_length);

	offset = descriptor[0];
	while (offset < descriptor_length) {
		const uint8_t item_length = descriptor[offset];
		const uint8_t item_type = descriptor[offset + 1U];

		if (item_length < 2U || item_length > descriptor_length - offset) {
			return USB_PROXY_DESC_ERR_LENGTH;
		}

		switch (item_type) {
		case USB_DESC_TYPE_INTERFACE:
			if (item_length < USB_DESC_MIN_INTERFACE_LENGTH) {
				return USB_PROXY_DESC_ERR_LENGTH;
			}
			if (configuration->interface_count >= USB_PROXY_DESCRIPTOR_MAX_INTERFACES) {
				return USB_PROXY_DESC_ERR_CAPACITY;
			}
			if (find_interface_mutable(configuration, descriptor[offset + 2U],
				descriptor[offset + 3U]) != NULL) {
				return USB_PROXY_DESC_ERR_DUPLICATE;
			}
			current_interface = &configuration->interfaces[configuration->interface_count++];
			current_interface->interface_number = descriptor[offset + 2U];
			current_interface->alternate_setting = descriptor[offset + 3U];
			current_interface->declared_endpoint_count = descriptor[offset + 4U];
			current_interface->interface_class = descriptor[offset + 5U];
			current_interface->interface_subclass = descriptor[offset + 6U];
			current_interface->interface_protocol = descriptor[offset + 7U];
			break;

		case USB_DESC_TYPE_ENDPOINT:
			if (item_length < USB_DESC_MIN_ENDPOINT_LENGTH ||
				current_interface == NULL) {
				return USB_PROXY_DESC_ERR_ENDPOINT;
			}
			if (configuration->endpoint_count >= USB_PROXY_DESCRIPTOR_MAX_ENDPOINTS) {
				return USB_PROXY_DESC_ERR_CAPACITY;
			}
			if ((descriptor[offset + 2U] & 0x0FU) == 0U ||
				(descriptor[offset + 2U] & 0x70U) != 0U) {
				return USB_PROXY_DESC_ERR_ENDPOINT;
			}
			{
				size_t i;
				for (i = 0U; i < configuration->endpoint_count; ++i) {
					const usb_proxy_endpoint_t *existing = &configuration->endpoints[i];
					if (existing->address == descriptor[offset + 2U] &&
						existing->interface_number == current_interface->interface_number &&
						existing->alternate_setting == current_interface->alternate_setting) {
						return USB_PROXY_DESC_ERR_DUPLICATE;
					}
				}
			}
			{
				usb_proxy_endpoint_t *endpoint =
					&configuration->endpoints[configuration->endpoint_count++];
				endpoint->interface_number = current_interface->interface_number;
				endpoint->alternate_setting = current_interface->alternate_setting;
				endpoint->address = descriptor[offset + 2U];
				endpoint->attributes = descriptor[offset + 3U];
				endpoint->max_packet_size = read_le16(&descriptor[offset + 4U]);
				endpoint->interval = descriptor[offset + 6U];
				current_interface->endpoint_count++;
			}
			break;

		case USB_DESC_TYPE_HID:
			if (item_length < USB_DESC_MIN_HID_LENGTH ||
				current_interface == NULL) {
				return USB_PROXY_DESC_ERR_INTERFACE;
			}
			if (configuration->hid_descriptor_count >= USB_PROXY_DESCRIPTOR_MAX_HID_DESCRIPTORS) {
				return USB_PROXY_DESC_ERR_CAPACITY;
			}
			{
				const uint8_t subordinate_count = descriptor[offset + 5U];
				size_t subordinate_offset = offset + USB_DESC_MIN_HID_LENGTH;
				size_t subordinate_end = offset + item_length;
				bool report_descriptor_found = false;

				if (item_length < USB_DESC_MIN_HID_LENGTH + (size_t)subordinate_count * 3U) {
					return USB_PROXY_DESC_ERR_LENGTH;
				}
				while (subordinate_count > 0U && subordinate_offset + 3U <= subordinate_end) {
					if (descriptor[subordinate_offset] == USB_DESC_TYPE_HID_REPORT) {
						usb_proxy_hid_descriptor_t *hid =
							&configuration->hid_descriptors[configuration->hid_descriptor_count++];
						hid->interface_number = current_interface->interface_number;
						hid->alternate_setting = current_interface->alternate_setting;
						hid->report_descriptor_length = read_le16(&descriptor[subordinate_offset + 1U]);
						report_descriptor_found = true;
						break;
					}
					subordinate_offset += 3U;
				}
				(void)report_descriptor_found;
			}
			break;

		case USB_DESC_TYPE_CONFIGURATION:
			return USB_PROXY_DESC_ERR_DUPLICATE;

		case USB_DESC_TYPE_INTERFACE_ASSOCIATION:
			current_interface = NULL;
			break;

		default:
			/* Preserve unknown class-specific descriptors in the raw byte stream. */
			break;
		}

		offset += item_length;
	}

	if (offset != descriptor_length || configuration->interface_count == 0U) {
		return USB_PROXY_DESC_ERR_INTERFACE;
	}

	{
		size_t i;
		bool interface_seen[256] = { false };
		for (i = 0U; i < configuration->interface_count; ++i) {
			const usb_proxy_interface_t *interface = &configuration->interfaces[i];
			if (interface->endpoint_count != interface->declared_endpoint_count) {
				return USB_PROXY_DESC_ERR_ENDPOINT;
			}
			if (!interface_seen[interface->interface_number]) {
				interface_seen[interface->interface_number] = true;
				configuration->unique_interface_count++;
			}
		}
	}
	if (configuration->unique_interface_count != configuration->declared_interface_count) {
		return USB_PROXY_DESC_ERR_INTERFACE;
	}

	return USB_PROXY_DESC_OK;
}
