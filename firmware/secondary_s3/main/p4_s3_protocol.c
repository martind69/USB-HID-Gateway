#include "p4_s3_protocol.h"
#include <string.h>
uint16_t p4_s3_crc16(const uint8_t*d,size_t n){uint16_t c=0xffff;for(size_t i=0;i<n;i++){c^=(uint16_t)d[i]<<8;for(int b=0;b<8;b++)c=(c&0x8000)?(uint16_t)((c<<1)^0x1021):(uint16_t)(c<<1);}return c;}
void p4_s3_frame_build(p4_s3_frame_t*f,uint8_t t,uint8_t s,const uint8_t*p,uint8_t n){memset(f,0,sizeof(*f));if(n>64)n=64;f->raw[0]=0xc5;f->raw[1]=0x5a;f->raw[2]=1;f->raw[3]=t;f->raw[4]=s;f->raw[5]=n;if(p&&n)memcpy(f->raw+6,p,n);uint16_t c=p4_s3_crc16(f->raw,70);f->raw[70]=(uint8_t)c;f->raw[71]=(uint8_t)(c>>8);}
bool p4_s3_frame_validate(const p4_s3_frame_t*f){return f&&f->raw[0]==0xc5&&f->raw[1]==0x5a&&f->raw[2]==1&&f->raw[5]<=64&&((uint16_t)f->raw[70]|((uint16_t)f->raw[71]<<8))==p4_s3_crc16(f->raw,70);}
