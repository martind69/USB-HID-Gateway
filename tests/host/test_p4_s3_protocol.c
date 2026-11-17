#include <assert.h>
#include "p4_s3_protocol.h"
int main(void){p4_s3_frame_t f;p4_s3_frame_build(&f,0x10,1,NULL,0);assert(p4_s3_frame_validate(&f));return 0;}
