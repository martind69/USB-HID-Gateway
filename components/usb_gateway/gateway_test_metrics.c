#include "gateway_test_metrics.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <string.h>
static gateway_link_metrics_t s_metrics;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
void gateway_link_metrics_reset(void){portENTER_CRITICAL(&s_lock);memset(&s_metrics,0,sizeof(s_metrics));portEXIT_CRITICAL(&s_lock);}
gateway_link_metrics_t gateway_link_metrics_snapshot(void){portENTER_CRITICAL(&s_lock);gateway_link_metrics_t copy=s_metrics;portEXIT_CRITICAL(&s_lock);return copy;}
void gateway_link_metrics_on_tx(void){portENTER_CRITICAL(&s_lock);s_metrics.frames_sent++;portEXIT_CRITICAL(&s_lock);}
void gateway_link_metrics_on_rx(uint32_t us){portENTER_CRITICAL(&s_lock);s_metrics.frames_received++;if(!s_metrics.latency_min_us||us<s_metrics.latency_min_us)s_metrics.latency_min_us=us;if(us>s_metrics.latency_max_us)s_metrics.latency_max_us=us;s_metrics.latency_sum_us+=us;portEXIT_CRITICAL(&s_lock);}
void gateway_link_metrics_on_timeout(void){portENTER_CRITICAL(&s_lock);s_metrics.timeouts++;portEXIT_CRITICAL(&s_lock);}
void gateway_link_metrics_on_recovery(void){portENTER_CRITICAL(&s_lock);s_metrics.recoveries++;portEXIT_CRITICAL(&s_lock);}
void gateway_link_metrics_on_crc_error(void){portENTER_CRITICAL(&s_lock);s_metrics.crc_errors++;portEXIT_CRITICAL(&s_lock);}
void gateway_link_metrics_on_sequence_error(void){portENTER_CRITICAL(&s_lock);s_metrics.sequence_errors++;portEXIT_CRITICAL(&s_lock);}
void gateway_link_metrics_on_queue_overflow(void){portENTER_CRITICAL(&s_lock);s_metrics.queue_overflows++;portEXIT_CRITICAL(&s_lock);}
