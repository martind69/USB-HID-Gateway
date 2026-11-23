/*
 * Host test for usb_mirror_descriptor.c
 * Build (from test/): gcc -std=c17 -Wall -Wextra -Werror -I../components/usb_mirror -I<stubs> \
 *     usb_mirror_descriptor_host_test.c ../components/usb_mirror/usb_mirror_descriptor.c
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "usb_mirror_descriptor.h"

/* G502 HERO 046D:C08B: IF0 boot mouse EP81/8, IF1 HID EP82/20, report 67 / 151 B. */
static const uint8_t G502_CONFIGURATION[] = {
    0x09, 0x02, 0x3B, 0x00, 0x02, 0x01, 0x00, 0xA0, 0x96,
    0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x02, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x43, 0x00,
    0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x01,
    0x09, 0x04, 0x01, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x97, 0x00,
    0x07, 0x05, 0x82, 0x03, 0x14, 0x00, 0x01,
};

/* G815 046D:C33F: IF0 boot keyboard EP81/8, IF1 HID EP82/64, report 65 / 188 B. */
static const uint8_t G815_CONFIGURATION[] = {
    0x09, 0x02, 0x3B, 0x00, 0x02, 0x01, 0x00, 0xA0, 0xFA,
    0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x41, 0x00,
    0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x01,
    0x09, 0x04, 0x01, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0xBC, 0x00,
    0x07, 0x05, 0x82, 0x03, 0x40, 0x00, 0x01,
};

int main(void)
{
    usb_mirror_layout_t layout;
    uint8_t buffer[128];

    assert(usb_mirror_parse_configuration(
        G502_CONFIGURATION, sizeof(G502_CONFIGURATION), 4U, 64U, &layout) == ESP_OK);
    assert(layout.interface_count == 2U && layout.hid_count == 2U);
    assert(layout.has_mouse && !layout.has_keyboard);
    assert(layout.hid_report_length[0] == 67U && layout.hid_report_length[1] == 151U);
    assert(layout.hid_descriptor_offset[0] == 18U && layout.hid_descriptor_offset[1] == 43U);
    assert(usb_mirror_hid_instance(&layout, 1U) == 1);
    assert(usb_mirror_hid_instance(&layout, 2U) == -1);

    assert(usb_mirror_parse_configuration(
        G815_CONFIGURATION, sizeof(G815_CONFIGURATION), 4U, 64U, &layout) == ESP_OK);
    assert(layout.has_keyboard && !layout.has_mouse);
    assert(layout.hid_report_length[1] == 188U);

    assert(usb_mirror_parse_configuration(
        G502_CONFIGURATION, sizeof(G502_CONFIGURATION), 1U, 64U, &layout) == ESP_ERR_NOT_SUPPORTED);
    assert(layout.has_mouse);
    assert(usb_mirror_parse_configuration(
        G815_CONFIGURATION, sizeof(G815_CONFIGURATION), 4U, 32U, &layout) == ESP_ERR_NOT_SUPPORTED);

    memcpy(buffer, G502_CONFIGURATION, sizeof(G502_CONFIGURATION));
    buffer[34 + 5] = 0x08;
    assert(usb_mirror_parse_configuration(
        buffer, sizeof(G502_CONFIGURATION), 4U, 64U, &layout) == ESP_ERR_NOT_SUPPORTED);
    assert(layout.has_mouse && layout.hid_count == 1U);

    memcpy(buffer, G502_CONFIGURATION, sizeof(G502_CONFIGURATION));
    buffer[27 + 3] = 0x02;
    assert(usb_mirror_parse_configuration(
        buffer, sizeof(G502_CONFIGURATION), 4U, 64U, &layout) == ESP_ERR_NOT_SUPPORTED);

    /* Report descriptor larger than the mirror capacity. */
    memcpy(buffer, G502_CONFIGURATION, sizeof(G502_CONFIGURATION));
    buffer[50] = 0x01;
    buffer[51] = 0x08;
    assert(usb_mirror_parse_configuration(
        buffer, sizeof(G502_CONFIGURATION), 4U, 64U, &layout) == ESP_ERR_NOT_SUPPORTED);

    assert(usb_mirror_parse_configuration(
        G502_CONFIGURATION, 30U, 4U, 64U, &layout) == ESP_ERR_INVALID_SIZE);
    memcpy(buffer, G502_CONFIGURATION, sizeof(G502_CONFIGURATION));
    buffer[9] = 0x00;
    assert(usb_mirror_parse_configuration(
        buffer, sizeof(G502_CONFIGURATION), 4U, 64U, &layout) == ESP_ERR_INVALID_SIZE);
    assert(usb_mirror_parse_configuration(NULL, 9U, 4U, 64U, &layout) == ESP_ERR_INVALID_ARG);

    const uint8_t product[] = {
        0x2E, 0x03,
        'G', 0, '5', 0, '0', 0, '2', 0, ' ', 0, 'H', 0, 'E', 0, 'R', 0, 'O', 0,
        ' ', 0, 'G', 0, 'a', 0, 'm', 0, 'i', 0, 'n', 0, 'g', 0, ' ', 0,
        'M', 0, 'o', 0, 'u', 0, 's', 0, 'e', 0,
    };
    char text[32];
    bool lossy = true;
    assert(usb_mirror_string_to_ascii(product, text, sizeof(text), &lossy) == 22U);
    assert(strcmp(text, "G502 HERO Gaming Mouse") == 0 && !lossy);
    const uint8_t umlaut[] = { 0x06, 0x03, 'A', 0, 0xFC, 0x00 };
    assert(usb_mirror_string_to_ascii(umlaut, text, sizeof(text), &lossy) == 2U);
    assert(strcmp(text, "A?") == 0 && lossy);
    char small[5];
    assert(usb_mirror_string_to_ascii(product, small, sizeof(small), &lossy) == 4U);
    assert(strcmp(small, "G502") == 0 && lossy);
    assert(usb_mirror_string_to_ascii(NULL, text, sizeof(text), &lossy) == 0U && text[0] == '\0');

    puts("usb_mirror_descriptor host test passed");
    return 0;
}
