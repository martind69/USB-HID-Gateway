#include "usb_mirror_descriptor.h"

#include <string.h>

#define DESCRIPTOR_TYPE_CONFIGURATION   0x02U
#define DESCRIPTOR_TYPE_STRING          0x03U
#define DESCRIPTOR_TYPE_INTERFACE       0x04U
#define DESCRIPTOR_TYPE_ENDPOINT        0x05U
#define DESCRIPTOR_TYPE_HID             0x21U
#define DESCRIPTOR_TYPE_HID_REPORT      0x22U

#define INTERFACE_CLASS_HID             0x03U
#define HID_SUBCLASS_BOOT               0x01U
#define HID_PROTOCOL_KEYBOARD           0x01U
#define HID_PROTOCOL_MOUSE              0x02U

#define ENDPOINT_TRANSFER_TYPE_MASK     0x03U
#define ENDPOINT_TRANSFER_INTERRUPT     0x03U

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8U));
}

esp_err_t usb_mirror_parse_configuration(
    const uint8_t *configuration,
    size_t length,
    size_t max_hid_interfaces,
    uint16_t max_packet_size,
    usb_mirror_layout_t *layout)
{
    if (configuration == NULL || layout == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(layout, 0, sizeof(*layout));
    if (max_hid_interfaces > USB_MIRROR_MAX_HID_INTERFACES) {
        max_hid_interfaces = USB_MIRROR_MAX_HID_INTERFACES;
    }
    if (length < 9U ||
        configuration[0] < 9U ||
        configuration[1] != DESCRIPTOR_TYPE_CONFIGURATION) {
        return ESP_ERR_INVALID_SIZE;
    }
    const size_t total_length = read_le16(&configuration[2]);
    if (total_length < 9U || total_length > length) {
        return ESP_ERR_INVALID_SIZE;
    }
    layout->interface_count = configuration[4];

    bool supported = true;
    bool in_hid_interface = false;
    int current_hid = -1;
    size_t offset = configuration[0];

    while (offset < total_length) {
        const uint8_t descriptor_length = configuration[offset];
        if (descriptor_length < 2U || offset + descriptor_length > total_length) {
            return ESP_ERR_INVALID_SIZE;
        }
        const uint8_t *descriptor = &configuration[offset];
        const uint8_t type = descriptor[1];

        if (type == DESCRIPTOR_TYPE_INTERFACE) {
            if (descriptor_length < 9U) {
                return ESP_ERR_INVALID_SIZE;
            }
            const uint8_t number = descriptor[2];
            const uint8_t alternate = descriptor[3];
            const uint8_t interface_class = descriptor[5];
            const uint8_t subclass = descriptor[6];
            const uint8_t protocol = descriptor[7];
            in_hid_interface = false;
            current_hid = -1;

            if (alternate != 0U) {
                supported = false;
            } else if (interface_class != INTERFACE_CLASS_HID) {
                supported = false;
            } else {
                in_hid_interface = true;
                if (subclass == HID_SUBCLASS_BOOT) {
                    if (protocol == HID_PROTOCOL_KEYBOARD) {
                        layout->has_keyboard = true;
                    } else if (protocol == HID_PROTOCOL_MOUSE) {
                        layout->has_mouse = true;
                    }
                }
                if (layout->hid_count < max_hid_interfaces) {
                    current_hid = layout->hid_count;
                    layout->hid_interface[current_hid] = number;
                    ++layout->hid_count;
                } else {
                    supported = false;
                }
            }
        } else if (type == DESCRIPTOR_TYPE_HID && in_hid_interface) {
            if (descriptor_length < 9U) {
                return ESP_ERR_INVALID_SIZE;
            }
            if (current_hid >= 0 && descriptor[6] == DESCRIPTOR_TYPE_HID_REPORT) {
                layout->hid_report_length[current_hid] = read_le16(&descriptor[7]);
                layout->hid_descriptor_offset[current_hid] = (uint16_t)offset;
            }
        } else if (type == DESCRIPTOR_TYPE_ENDPOINT) {
            if (descriptor_length < 7U) {
                return ESP_ERR_INVALID_SIZE;
            }
            const uint8_t attributes = descriptor[3];
            const uint16_t packet_size = (uint16_t)(read_le16(&descriptor[4]) & 0x07FFU);
            if ((attributes & ENDPOINT_TRANSFER_TYPE_MASK) != ENDPOINT_TRANSFER_INTERRUPT ||
                packet_size > max_packet_size) {
                supported = false;
            }
        }
        offset += descriptor_length;
    }

    for (uint8_t index = 0U; index < layout->hid_count; ++index) {
        if (layout->hid_report_length[index] == 0U ||
            layout->hid_report_length[index] > USB_MIRROR_REPORT_DESC_CAPACITY) {
            supported = false;
        }
    }
    if (layout->hid_count == 0U) {
        supported = false;
    }
    return supported ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
}

int usb_mirror_hid_instance(
    const usb_mirror_layout_t *layout,
    uint8_t interface_number)
{
    if (layout == NULL) {
        return -1;
    }
    for (uint8_t index = 0U; index < layout->hid_count; ++index) {
        if (layout->hid_interface[index] == interface_number) {
            return (int)index;
        }
    }
    return -1;
}

size_t usb_mirror_string_to_ascii(
    const uint8_t *string_descriptor,
    char *output,
    size_t capacity,
    bool *lossy)
{
    bool replaced = false;
    size_t written = 0U;
    if (output == NULL || capacity == 0U) {
        if (lossy != NULL) {
            *lossy = false;
        }
        return 0U;
    }
    output[0] = '\0';
    if (string_descriptor == NULL ||
        string_descriptor[0] < 2U ||
        string_descriptor[1] != DESCRIPTOR_TYPE_STRING) {
        if (lossy != NULL) {
            *lossy = false;
        }
        return 0U;
    }
    const size_t characters = ((size_t)string_descriptor[0] - 2U) / 2U;
    for (size_t index = 0U; index < characters; ++index) {
        if (written + 1U >= capacity) {
            replaced = true;
            break;
        }
        const uint16_t character = read_le16(&string_descriptor[2U + 2U * index]);
        if (character >= 0x20U && character < 0x7FU) {
            output[written++] = (char)character;
        } else {
            output[written++] = '?';
            replaced = true;
        }
    }
    output[written] = '\0';
    if (lossy != NULL) {
        *lossy = replaced;
    }
    return written;
}
