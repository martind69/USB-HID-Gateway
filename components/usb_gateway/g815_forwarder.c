#include "g815_forwarder.h"

#include "p4_s3_link.h"
#include "p4_s3_protocol.h"

#define G815_IF0_REPORT_LENGTH  8U

void g815_forwarder_init(void)
{
}

bool g815_forwarder_submit(
    uint8_t interface_number,
    const uint8_t *report,
    size_t length)
{
    if (report == NULL) {
        return false;
    }
    if (interface_number == 0U && length == G815_IF0_REPORT_LENGTH) {
        return p4_s3_link_enqueue(P4_S3_MSG_G815_IF0_INPUT, report, length);
    }
    if (interface_number == 1U && length > 0U && length <= P4_S3_PAYLOAD_SIZE) {
        return p4_s3_link_enqueue(P4_S3_MSG_G815_IF1_INPUT, report, length);
    }
    return false;
}
