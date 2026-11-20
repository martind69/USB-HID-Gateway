#include "g815_forwarder.h"
#include "p4_s3_link.h"
void g815_forwarder_init(void){}bool g815_forwarder_submit(uint8_t i,const uint8_t*r,size_t n){if(i==0&&n==8)return p4_s3_link_enqueue(P4_S3_MSG_G815_IF0_INPUT,r,n);if(i==1&&n>0&&n<=64)return p4_s3_link_enqueue(P4_S3_MSG_G815_IF1_INPUT,r,n);return false;}
