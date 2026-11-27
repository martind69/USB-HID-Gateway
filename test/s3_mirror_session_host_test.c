/*
 * Host test: P4 descriptor chunking -> S3 session staging -> mirror begin.
 * The mirror device and the uplink are replaced by recording fakes.
 *
 * Build (from test/):
 *   gcc -std=c17 -Wall -Wextra -Werror -I../components/usb_mirror \
 *       -I../firmware/secondary_s3/main -I<stubs> s3_mirror_session_host_test.c \
 *       ../firmware/secondary_s3/main/s3_mirror_session.c \
 *       ../components/usb_mirror/usb_mirror_descriptor.c
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "p4_s3_protocol.h"
#include "s3_mirror_session.h"
#include "s3_proxy_reports.h"
#include "usb_mirror_device.h"

/* ---- fakes ------------------------------------------------------------ */

int log_stub(const char *format, ...) { (void)format; return 0; }
const char *esp_err_to_name(esp_err_t error) { (void)error; return "error"; }
int64_t esp_timer_get_time(void) { return 5000000; }

esp_err_t s3_proxy_send_set_report(uint8_t i, uint8_t r, uint8_t t, const uint8_t *d, size_t l)
{
    (void)i; (void)r; (void)t; (void)d; (void)l;
    return ESP_OK;
}

static uint8_t g_device[18];
static uint8_t g_config[512];
static size_t g_config_length;
static char g_strings[USB_MIRROR_STRING_COUNT][USB_MIRROR_STRING_CAPACITY];
static uint8_t g_report[2][512];
static size_t g_report_length[2];
static uint8_t g_session;
static bool g_connected;
static usb_mirror_layout_t g_layout;

esp_err_t usb_mirror_device_init(const usb_mirror_device_config_t *c) { (void)c; return ESP_OK; }

esp_err_t usb_mirror_device_begin(uint8_t session, const uint8_t d[18], const uint8_t *c,
    size_t l, const char *const s[USB_MIRROR_STRING_COUNT])
{
    if (usb_mirror_parse_configuration(c, l, 2U, 64U, &g_layout) != ESP_OK) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    g_session = session;
    memcpy(g_device, d, 18);
    memcpy(g_config, c, l);
    g_config_length = l;
    for (size_t i = 1; i < USB_MIRROR_STRING_COUNT; ++i) {
        strcpy(g_strings[i], s[i] != NULL ? s[i] : "");
    }
    g_report_length[0] = g_report_length[1] = 0U;
    g_connected = false;
    return ESP_OK;
}

void usb_mirror_device_set_report_descriptor(uint8_t s, uint8_t iface, const uint8_t *d, size_t l)
{
    assert(s == g_session);
    const int instance = usb_mirror_hid_instance(&g_layout, iface);
    assert(instance >= 0);
    memcpy(g_report[instance], d, l);
    g_report_length[instance] = l;
    g_connected = g_report_length[0] > 0U && g_report_length[1] > 0U;
}

bool usb_mirror_device_input(uint8_t s, uint8_t i, const uint8_t *r, size_t l)
{
    (void)i; (void)r; (void)l;
    return g_connected && s == g_session;
}

void usb_mirror_device_end(uint8_t s) { (void)s; g_connected = false; g_session = 0U; }
bool usb_mirror_device_connected(void) { return g_connected; }

void usb_mirror_device_get_stats(usb_mirror_device_stats_t *stats)
{
    memset(stats, 0, sizeof(*stats));
    stats->session = g_session;
    stats->state = g_connected ? P4_S3_MIRROR_STATE_CONNECTED : P4_S3_MIRROR_STATE_IDLE;
}

/* ---- P4 side encoder (same chunking as s3_mirror.c send_descriptor) ---- */

static void send_descriptor(uint8_t kind, uint8_t index, const uint8_t *data, size_t length)
{
    size_t offset = 0U;
    do {
        const size_t chunk = (length - offset) > P4_S3_DESCRIPTOR_DATA_SIZE
            ? P4_S3_DESCRIPTOR_DATA_SIZE : length - offset;
        p4_s3_descriptor_chunk_t packet = {
            .kind = kind, .index = index, .offset = (uint16_t)offset,
            .total_length = (uint16_t)length, .data_length = (uint8_t)chunk,
        };
        memcpy(packet.data, &data[offset], chunk);
        assert(s3_mirror_session_descriptor((const uint8_t *)&packet,
            P4_S3_DESCRIPTOR_HEADER_SIZE + chunk) == P4_S3_STATUS_OK);
        offset += chunk;
    } while (offset < length);
}

/* G815 046D:C33F, IF0 boot keyboard 65 B, IF1 HID 188 B, EP 8 / 64 B. */
static const uint8_t CONFIG[] = {
    0x09, 0x02, 0x3B, 0x00, 0x02, 0x01, 0x00, 0xA0, 0xFA,
    0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x41, 0x00,
    0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x01,
    0x09, 0x04, 0x01, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0xBC, 0x00,
    0x07, 0x05, 0x82, 0x03, 0x40, 0x00, 0x01,
};
static const uint8_t DEVICE[18] = {
    18, 1, 0x00, 0x02, 0, 0, 0, 64, 0x6D, 0x04, 0x3F, 0xC3, 0x02, 0x31, 1, 2, 3, 1,
};

int main(void)
{
    uint8_t report0[65], report1[188];
    for (size_t i = 0; i < sizeof(report0); ++i) report0[i] = (uint8_t)i;
    for (size_t i = 0; i < sizeof(report1); ++i) report1[i] = (uint8_t)(0xFF - i);

    assert(s3_mirror_session_init() == ESP_OK);

    /* No session yet. */
    assert(s3_mirror_session_commit() == P4_S3_STATUS_NO_SESSION);
    uint8_t key[8] = { 0 };
    assert(s3_mirror_session_input(0, key, 8) == P4_S3_STATUS_NO_SESSION);

    /* Full session as sent by the P4. */
    const p4_s3_mirror_begin_t begin = { .session = 7 };
    assert(s3_mirror_session_begin((const uint8_t *)&begin, sizeof(begin)) == P4_S3_STATUS_OK);
    assert(s3_mirror_session_commit() == P4_S3_STATUS_INVALID_STATE);
    send_descriptor(P4_S3_DESCRIPTOR_DEVICE, 0, DEVICE, sizeof(DEVICE));
    send_descriptor(P4_S3_DESCRIPTOR_CONFIGURATION, 0, CONFIG, sizeof(CONFIG));
    send_descriptor(P4_S3_DESCRIPTOR_STRING, 1, (const uint8_t *)"Logitech", 8);
    send_descriptor(P4_S3_DESCRIPTOR_STRING, 2, (const uint8_t *)"G815 RGB MECHANICAL GAMING KEYB", 31);
    send_descriptor(P4_S3_DESCRIPTOR_STRING, 3, (const uint8_t *)"1234ABCD", 8);
    send_descriptor(P4_S3_DESCRIPTOR_REPORT, 0, report0, sizeof(report0));
    send_descriptor(P4_S3_DESCRIPTOR_REPORT, 1, report1, sizeof(report1));
    assert(s3_mirror_session_commit() == P4_S3_STATUS_OK);

    assert(g_session == 7 && g_connected);
    assert(memcmp(g_device, DEVICE, 18) == 0);
    assert(g_config_length == sizeof(CONFIG) && memcmp(g_config, CONFIG, sizeof(CONFIG)) == 0);
    assert(strcmp(g_strings[1], "Logitech") == 0);
    assert(strcmp(g_strings[2], "G815 RGB MECHANICAL GAMING KEYB") == 0);
    assert(strcmp(g_strings[3], "1234ABCD") == 0);
    assert(g_report_length[0] == 65 && memcmp(g_report[0], report0, 65) == 0);
    assert(g_report_length[1] == 188 && memcmp(g_report[1], report1, 188) == 0);
    assert(s3_mirror_session_input(0, key, 8) == P4_S3_STATUS_OK);

    /* Stats. */
    p4_s3_mirror_stats_t stats;
    s3_mirror_session_stats(&stats);
    assert(stats.status == P4_S3_STATUS_OK && stats.session == 7 &&
        stats.state == P4_S3_MIRROR_STATE_CONNECTED && stats.uptime_ms == 5000U);

    /* Protocol violations. */
    assert(s3_mirror_session_begin((const uint8_t *)&begin, sizeof(begin)) == P4_S3_STATUS_OK);
    p4_s3_descriptor_chunk_t bad = {
        .kind = P4_S3_DESCRIPTOR_CONFIGURATION, .offset = 56, .total_length = 59, .data_length = 3,
    };
    assert(s3_mirror_session_descriptor((const uint8_t *)&bad, 11) == P4_S3_STATUS_INVALID_LENGTH);
    bad = (p4_s3_descriptor_chunk_t) { .kind = P4_S3_DESCRIPTOR_DEVICE, .total_length = 17, .data_length = 17 };
    assert(s3_mirror_session_descriptor((const uint8_t *)&bad, 25) == P4_S3_STATUS_INVALID_LENGTH);
    bad = (p4_s3_descriptor_chunk_t) { .kind = P4_S3_DESCRIPTOR_CONFIGURATION, .total_length = 600, .data_length = 4 };
    assert(s3_mirror_session_descriptor((const uint8_t *)&bad, 12) == P4_S3_STATUS_INVALID_LENGTH);
    bad = (p4_s3_descriptor_chunk_t) { .kind = 9, .total_length = 4, .data_length = 4 };
    assert(s3_mirror_session_descriptor((const uint8_t *)&bad, 12) == P4_S3_STATUS_UNSUPPORTED);
    bad = (p4_s3_descriptor_chunk_t) { .kind = P4_S3_DESCRIPTOR_STRING, .index = 4, .total_length = 1, .data_length = 1 };
    assert(s3_mirror_session_descriptor((const uint8_t *)&bad, 9) == P4_S3_STATUS_INVALID_LENGTH);
    assert(s3_mirror_session_descriptor((const uint8_t *)&bad, 10) == P4_S3_STATUS_INVALID_LENGTH);

    /* Report descriptor missing -> commit refused, P4 retries. */
    send_descriptor(P4_S3_DESCRIPTOR_DEVICE, 0, DEVICE, sizeof(DEVICE));
    send_descriptor(P4_S3_DESCRIPTOR_CONFIGURATION, 0, CONFIG, sizeof(CONFIG));
    send_descriptor(P4_S3_DESCRIPTOR_REPORT, 0, report0, sizeof(report0));
    assert(s3_mirror_session_commit() == P4_S3_STATUS_INVALID_STATE);

    /* End. */
    assert(s3_mirror_session_end() == P4_S3_STATUS_OK);
    assert(!g_connected);
    assert(s3_mirror_session_commit() == P4_S3_STATUS_NO_SESSION);

    puts("s3_mirror_session host test passed");
    return 0;
}
