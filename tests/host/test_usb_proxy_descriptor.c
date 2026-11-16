#include "usb_proxy_descriptor.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t s_configuration[] = {
	9, 0x02, 43, 0, 1, 1, 0, 0x80, 50,
	9, 0x04, 0, 0, 1, 0x03, 0x01, 0x01, 0,
	9, 0x21, 0x11, 0x01, 0, 1, 0x22, 63, 0,
	7, 0x05, 0x81, 0x03, 8, 0, 10,
	9, 0x04, 0, 1, 1, 0x03, 0x00, 0x00, 0,
	7, 0x05, 0x81, 0x03, 8, 0, 1
};

static void test_valid_configuration(void)
{
	usb_proxy_configuration_t configuration;
	const usb_proxy_endpoint_t *endpoint;
	const usb_proxy_interface_t *interface;
	usb_proxy_descriptor_result_t result;

	result = usb_proxy_configuration_parse(
		&configuration, s_configuration, sizeof(s_configuration));
	assert(result == USB_PROXY_DESC_OK);
	assert(configuration.total_length == sizeof(s_configuration));
	assert(configuration.configuration_value == 1U);
	assert(configuration.declared_interface_count == 1U);
	assert(configuration.unique_interface_count == 1U);
	assert(configuration.interface_count == 2U);
	assert(configuration.endpoint_count == 2U);
	assert(configuration.hid_descriptor_count == 1U);
	assert(configuration.hid_descriptors[0].report_descriptor_length == 63U);
	assert(memcmp(configuration.raw, s_configuration, sizeof(s_configuration)) == 0);

	interface = usb_proxy_configuration_find_interface(&configuration, 0U, 1U);
	assert(interface != NULL);
	assert(interface->interface_class == 0x03U);
	assert(interface->endpoint_count == 1U);

	endpoint = usb_proxy_configuration_find_endpoint(&configuration, 0x81U, 0U, 0U);
	assert(endpoint != NULL);
	assert(endpoint->max_packet_size == 8U);
	assert(endpoint->interval == 10U);

	endpoint = usb_proxy_configuration_find_endpoint(&configuration, 0x81U, 0U, 1U);
	assert(endpoint != NULL);
	assert(endpoint->interval == 1U);
}

static void test_rejects_incomplete_stream(void)
{
	usb_proxy_configuration_t configuration;
	usb_proxy_descriptor_result_t result;

	result = usb_proxy_configuration_parse(
		&configuration, s_configuration, sizeof(s_configuration) - 1U);
	assert(result == USB_PROXY_DESC_ERR_LENGTH);
}

static void test_rejects_endpoint_count_mismatch(void)
{
	uint8_t descriptor[sizeof(s_configuration)];
	usb_proxy_configuration_t configuration;

	memcpy(descriptor, s_configuration, sizeof(descriptor));
	descriptor[13] = 2U;
	assert(usb_proxy_configuration_parse(
		&configuration, descriptor, sizeof(descriptor)) == USB_PROXY_DESC_ERR_ENDPOINT);
}

static void test_rejects_duplicate_endpoint_in_alternate_setting(void)
{
	uint8_t descriptor[sizeof(s_configuration) + 7U];
	usb_proxy_configuration_t configuration;

	memcpy(descriptor, s_configuration, sizeof(s_configuration));
	descriptor[2] = (uint8_t)sizeof(descriptor);
	descriptor[3] = 0U;
	memcpy(&descriptor[sizeof(s_configuration)], &s_configuration[27], 7U);
	descriptor[sizeof(s_configuration) + 2U] = 0x81U;
	descriptor[sizeof(s_configuration) + 3U] = 0x03U;
	descriptor[sizeof(s_configuration) + 4U] = 8U;
	descriptor[sizeof(s_configuration) + 5U] = 0U;
	descriptor[sizeof(s_configuration) + 6U] = 10U;

	assert(usb_proxy_configuration_parse(
		&configuration, descriptor, sizeof(descriptor)) == USB_PROXY_DESC_ERR_DUPLICATE);
}

static void test_rejects_invalid_endpoint_zero(void)
{
	uint8_t descriptor[sizeof(s_configuration)];
	usb_proxy_configuration_t configuration;

	memcpy(descriptor, s_configuration, sizeof(descriptor));
	descriptor[29] = 0x80U;
	assert(usb_proxy_configuration_parse(
		&configuration, descriptor, sizeof(descriptor)) == USB_PROXY_DESC_ERR_ENDPOINT);
}

int main(void)
{
	test_valid_configuration();
	test_rejects_incomplete_stream();
	test_rejects_endpoint_count_mismatch();
	test_rejects_duplicate_endpoint_in_alternate_setting();
	test_rejects_invalid_endpoint_zero();

	puts("USB proxy descriptor tests passed");
	return 0;
}
