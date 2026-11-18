#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
void g815_forwarder_init(void);bool g815_forwarder_submit(uint8_t interface_number,const uint8_t*report,size_t length);
